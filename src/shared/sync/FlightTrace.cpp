// shared_cockpit - flight trace (debug, 0.8.4; Follow columns 0.9)

#include "shared/sync/FlightTrace.h"

#include <ctime>

#include "fmt/format.h"

#include "shared/log/Log.h"
#include "shared/util/Utils.h"

namespace sc::sync {

namespace {
constexpr auto kInterval = std::chrono::milliseconds(100);
constexpr const char* kHeader =
    "wall_ms,t_s,role,lat,lon,heading,gs_ms,on_ground,frame_s,"
    "in_pitch,in_roll,in_yaw,total_pitch,total_roll,total_yaw,nosewheel_deg,"
    "brake_l,brake_r,parking,throttle,throttle_use,rpm,prop_mode,elev_trim,flap_req,"
    "ov_joystick,ov_throttles,ov_toe_brakes,inputs_age_ms,held_per_s,"
    "mode,follow,gap_m,gap_deg,state_delay_ms\n";
}

FlightTrace::FlightTrace(net::SessionHub& hub, std::filesystem::path folder, const settings::Settings& settings)
    : mHub(hub), mFolder(std::move(folder)), mSettings(settings)
{
    mHub.Flight_cai().tracePath = Utils::Utf8_cai(mFolder / "shared_cockpit_trace.csv");
    if (mSettings.debug.flightTrace)
        LOG_INFO("flight trace: on - {} while a session runs", Utils::Utf8_cai(mFolder / "shared_cockpit_trace.csv"));
}

FlightTrace::~FlightTrace()
{
    Close_cai();
}

// A float, 0 when missing.
float FlightTrace::Float_cai(XPLMDataRef ref, int index)
{
    if (!ref)
        return 0.0f;
    if (index < 0)
        return XPLMGetDataf(ref);
    float value = 0.0f;
    XPLMGetDatavf(ref, &value, index, 1);
    return value;
}

// Finds the datarefs.
void FlightTrace::FindRefs_cai()
{
    if (mFound)
        return;
    mFound = true;
    auto& r = mRefs;
    r.lat = XPLMFindDataRef("sim/flightmodel/position/latitude");
    r.lon = XPLMFindDataRef("sim/flightmodel/position/longitude");
    r.psi = XPLMFindDataRef("sim/flightmodel/position/true_psi");
    r.gs = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
    r.onGround = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
    r.frame = XPLMFindDataRef("sim/operation/misc/frame_rate_period");
    r.inPitch = XPLMFindDataRef("sim/joystick/yoke_pitch_ratio");
    r.inRoll = XPLMFindDataRef("sim/joystick/yoke_roll_ratio");
    r.inYaw = XPLMFindDataRef("sim/joystick/yoke_heading_ratio");
    r.totalPitch = XPLMFindDataRef("sim/cockpit2/controls/total_pitch_ratio");
    r.totalRoll = XPLMFindDataRef("sim/cockpit2/controls/total_roll_ratio");
    r.totalYaw = XPLMFindDataRef("sim/cockpit2/controls/total_heading_ratio");
    r.steer = XPLMFindDataRef("sim/flightmodel2/gear/tire_steer_actual_deg");
    r.brakeL = XPLMFindDataRef("sim/cockpit2/controls/left_brake_ratio");
    r.brakeR = XPLMFindDataRef("sim/cockpit2/controls/right_brake_ratio");
    r.parking = XPLMFindDataRef("sim/cockpit2/controls/parking_brake_ratio");
    r.throttle = XPLMFindDataRef("sim/cockpit2/engine/actuators/throttle_ratio");
    r.throttleUse = XPLMFindDataRef("sim/flightmodel/engine/ENGN_thro_use");
    r.rpm = XPLMFindDataRef("sim/cockpit2/engine/indicators/engine_speed_rpm");
    r.propMode = XPLMFindDataRef("sim/cockpit2/engine/actuators/prop_mode");
    r.trim = XPLMFindDataRef("sim/cockpit2/controls/elevator_trim");
    r.flap = XPLMFindDataRef("sim/cockpit2/controls/flap_handle_request_ratio");
    r.ovJoystick = XPLMFindDataRef("sim/operation/override/override_joystick");
    r.ovThrottles = XPLMFindDataRef("sim/operation/override/override_throttles");
    r.ovToeBrakes = XPLMFindDataRef("sim/operation/override/override_toe_brakes");
}

// Opens a new file.
void FlightTrace::Open_cai()
{
    const std::filesystem::path path = mFolder / "shared_cockpit_trace.csv";
    std::error_code error;
    if (std::filesystem::exists(path, error))
        std::filesystem::rename(path, mFolder / "shared_cockpit_trace.1.csv", error);
    mFile = std::fopen(Utils::Utf8_cai(path).c_str(), "w");
    if (!mFile) {
        LOG_WARN("flight trace: cannot write {}", Utils::Utf8_cai(path));
        mFailed = true;
        return;
    }
    std::fputs(kHeader, mFile);
    mHub.Flight_cai().traceWriting = true;
    mHub.Flight_cai().traceBytes = 0;
    mStart = Clock::now();
    mNext = mStart;
    LOG_INFO("flight trace: writing {}", Utils::Utf8_cai(path));
}

// Closes the file.
void FlightTrace::Close_cai()
{
    if (!mFile)
        return;
    std::fclose(mFile);
    mFile = nullptr;
    mHub.Flight_cai().traceWriting = false;
    LOG_INFO("flight trace: closed");
}

// From the flight loop.
void FlightTrace::Tick_cai()
{
    const net::NetSession& session = mHub.Session_cai();
    if (!mSettings.debug.flightTrace) {
        Close_cai();
        mFailed = false;
        return;
    }
    if (!session.Active_cai()) {
        Close_cai();
        return;
    }
    if (mFailed)
        return;
    if (!mFile) {
        FindRefs_cai();
        Open_cai();
        if (!mFile)
            return;
    }
    const auto now = Clock::now();
    if (now < mNext)
        return;
    mNext = now + kInterval;
    const auto& r = mRefs;
    const net::FlightStats& flight = mHub.Flight_cai();
    const long long wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch()).count();
    const double t = std::chrono::duration<double>(now - mStart).count();
    const long long inputsAge = flight.inputsAt == Clock::time_point{} ? -1
        : std::chrono::duration_cast<std::chrono::milliseconds>(now - flight.inputsAt).count();
    int propMode = 0;
    if (r.propMode)
        XPLMGetDatavi(r.propMode, &propMode, 0, 1);
    const std::string row = fmt::format(
        "{},{:.2f},{},{:.8f},{:.8f},{:.2f},{:.3f},{},{:.4f},"
        "{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.2f},"
        "{:.3f},{:.3f},{:.3f},{:.4f},{:.4f},{:.0f},{},{:.4f},{:.3f},"
        "{},{},{},{},{},"
        "{},{},{:.2f},{:.2f},{:.0f}\n",
        wallMs, t, flight.iAmPic ? "PIC" : "follower",
        r.lat ? XPLMGetDatad(r.lat) : 0.0, r.lon ? XPLMGetDatad(r.lon) : 0.0, Float_cai(r.psi), Float_cai(r.gs),
        r.onGround ? XPLMGetDatai(r.onGround) : 0, Float_cai(r.frame),
        Float_cai(r.inPitch), Float_cai(r.inRoll), Float_cai(r.inYaw), Float_cai(r.totalPitch), Float_cai(r.totalRoll),
        Float_cai(r.totalYaw), Float_cai(r.steer, 0),
        Float_cai(r.brakeL), Float_cai(r.brakeR), Float_cai(r.parking), Float_cai(r.throttle, 0), Float_cai(r.throttleUse, 0),
        Float_cai(r.rpm, 0), propMode, Float_cai(r.trim), Float_cai(r.flap),
        r.ovJoystick ? XPLMGetDatai(r.ovJoystick) : -1, r.ovThrottles ? XPLMGetDatai(r.ovThrottles) : -1,
        r.ovToeBrakes ? XPLMGetDatai(r.ovToeBrakes) : -1, inputsAge, flight.heldPerSecond,
        flight.mode, flight.iAmPic ? "lead" : flight.farAway ? "far" : flight.holding ? "hold" : flight.following ? (flight.strict ? "strict" : "on") : "off",
        flight.gapM, flight.gapDeg, flight.delayMs);
    std::fputs(row.c_str(), mFile);
    mHub.Flight_cai().traceBytes += row.size();
}

} // namespace sc::sync
