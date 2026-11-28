// shared_cockpit - flying together, way 1: the PIC's inputs for everybody (0.8)

#include "shared/sync/FlightInputs.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "fmt/format.h"

#include "shared/log/Log.h"
#include "shared/net/Protocol.h"

namespace sc::sync {

namespace {

constexpr std::uint16_t kWriteHands = 1u << 15;   // yoke, rudder, toe brakes: always the PIC's

// The datarefs of the inputs (the cockpit's view of them; DatarefSync leaves these alone).
constexpr std::string_view kOwned[] = {
    "sim/joystick/yoke_pitch_ratio", "sim/joystick/yoke_roll_ratio", "sim/joystick/yoke_heading_ratio",
    "sim/cockpit2/controls/yoke_pitch_ratio", "sim/cockpit2/controls/yoke_roll_ratio", "sim/cockpit2/controls/yoke_heading_ratio",
    "sim/cockpit2/controls/left_brake_ratio", "sim/cockpit2/controls/right_brake_ratio", "sim/cockpit2/controls/parking_brake_ratio",
    "sim/cockpit2/engine/actuators/throttle_ratio", "sim/cockpit2/engine/actuators/prop_ratio",
    "sim/cockpit2/engine/actuators/mixture_ratio", "sim/cockpit2/engine/actuators/throttle_ratio_all",
    "sim/cockpit2/engine/actuators/prop_ratio_all", "sim/cockpit2/engine/actuators/mixture_ratio_all",
    "sim/cockpit2/controls/speedbrake_ratio",
    "sim/cockpit2/controls/elevator_trim", "sim/cockpit2/controls/aileron_trim", "sim/cockpit2/controls/rudder_trim",
    "sim/cockpit2/controls/flap_handle_request_ratio", "sim/cockpit2/controls/flap_ratio",
    "sim/cockpit2/engine/actuators/prop_mode",
};
constexpr float kChange = 1e-4f;   // (also the copilot axes)                  // smaller differences are not a change
constexpr float kHeldTolerance = 2e-3f;           // a lever this far off the written value: hardware moved it

// Two input sets differ enough to send.
bool Differs_cai(const net::FlightInputs& a, const net::FlightInputs& b)
{
    auto lmbda_near = [](float x, float y) { return std::fabs(x - y) <= kChange; };
    auto lmbda_nearAll = [&lmbda_near](const std::vector<float>& x, const std::vector<float>& y) {
        if (x.size() != y.size())
            return false;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (!lmbda_near(x[i], y[i]))
                return false;
        return true;
    };
    return !(lmbda_near(a.yokePitch, b.yokePitch) && lmbda_near(a.yokeRoll, b.yokeRoll) && lmbda_near(a.yokeHeading, b.yokeHeading) &&
             lmbda_near(a.brakeLeft, b.brakeLeft) && lmbda_near(a.brakeRight, b.brakeRight) &&
             lmbda_near(a.parkingBrake, b.parkingBrake) && lmbda_near(a.speedbrake, b.speedbrake) &&
             lmbda_near(a.trimElevator, b.trimElevator) && lmbda_near(a.trimAileron, b.trimAileron) &&
             lmbda_near(a.trimRudder, b.trimRudder) && lmbda_near(a.flapRequest, b.flapRequest) && a.propMode == b.propMode && lmbda_nearAll(a.throttle, b.throttle) && lmbda_nearAll(a.prop, b.prop) &&
             lmbda_nearAll(a.mixture, b.mixture));
}

} // namespace

FlightInputs::FlightInputs(net::SessionHub& hub) : mHub(hub)
{
}

FlightInputs::~FlightInputs()
{
    Stop_cai();
}

// True for the datarefs this way shares itself.
bool FlightInputs::OwnsDataref_cai(std::string_view name)
{
    return std::find(std::begin(kOwned), std::end(kOwned), name) != std::end(kOwned);
}

// Finds the datarefs (once).
void FlightInputs::FindRefs_cai()
{
    if (mFound)
        return;
    mFound = true;
    net::FlightStats& stats = mHub.Flight_cai();
    stats.missing.clear();
    auto lmbda_find = [&stats](const char* name, bool needed) {
        XPLMDataRef ref = XPLMFindDataRef(name);
        if (!ref && needed)
            stats.missing.push_back(name);
        if (!ref)
            LOG_INFO("flight inputs: {} not in this X-Plane{}", name, needed ? "" : " (optional)");
        return ref;
    };
    Refs& r = mRefs;
    r.yokePitch = lmbda_find("sim/joystick/yoke_pitch_ratio", true);
    r.yokeRoll = lmbda_find("sim/joystick/yoke_roll_ratio", true);
    r.yokeHeading = lmbda_find("sim/joystick/yoke_heading_ratio", true);
    r.brakeLeft = lmbda_find("sim/cockpit2/controls/left_brake_ratio", true);
    r.brakeRight = lmbda_find("sim/cockpit2/controls/right_brake_ratio", true);
    r.parking = lmbda_find("sim/cockpit2/controls/parking_brake_ratio", true);
    r.speedbrake = lmbda_find("sim/cockpit2/controls/speedbrake_ratio", true);
    r.trimElevator = lmbda_find("sim/cockpit2/controls/elevator_trim", true);
    r.trimAileron = lmbda_find("sim/cockpit2/controls/aileron_trim", true);
    r.trimRudder = lmbda_find("sim/cockpit2/controls/rudder_trim", true);
    r.flapRequest = lmbda_find("sim/cockpit2/controls/flap_handle_request_ratio", true);
    r.propMode = lmbda_find("sim/cockpit2/engine/actuators/prop_mode", true);
    r.throttle = lmbda_find("sim/cockpit2/engine/actuators/throttle_ratio", true);
    r.prop = lmbda_find("sim/cockpit2/engine/actuators/prop_ratio", true);
    r.mixture = lmbda_find("sim/cockpit2/engine/actuators/mixture_ratio", true);
    r.throttleUse = lmbda_find("sim/flightmodel/engine/ENGN_thro_use", false);
    r.engines = lmbda_find("sim/aircraft/engine/acf_num_engines", true);
    r.overrideJoystick = lmbda_find("sim/operation/override/override_joystick", true);
    r.overrideToeBrakes = lmbda_find("sim/operation/override/override_toe_brakes", false);
    r.overrideThrottles = lmbda_find("sim/operation/override/override_throttles", false);
    r.isHelicopter = lmbda_find("sim/aircraft2/metadata/is_helicopter", false);
    r.copPitch = lmbda_find("sim/cockpit2/controls/yoke_pitch_ratio_copilot", false);
    r.copRoll = lmbda_find("sim/cockpit2/controls/yoke_roll_ratio_copilot", false);
    r.copHeading = lmbda_find("sim/cockpit2/controls/yoke_heading_ratio_copilot", false);
    r.copOverride[0] = lmbda_find("sim/operation/override/override_joystick_pitch_copilot", false);
    r.copOverride[1] = lmbda_find("sim/operation/override/override_joystick_roll_copilot", false);
    r.copOverride[2] = lmbda_find("sim/operation/override/override_joystick_heading_copilot", false);
    const bool helicopter = r.isHelicopter && XPLMGetDatai(r.isHelicopter) != 0;
    if (helicopter)
        r.overridePropPitch = lmbda_find("sim/operation/override/override_prop_pitch", false);
    LOG_INFO("flight inputs: {} engine(s){}; overrides: joystick {}, toe brakes {}, throttles {}{}", Engines_cai(),
             helicopter ? ", a helicopter (collective = prop lever)" : "", r.overrideJoystick ? "yes" : "no",
             r.overrideToeBrakes ? "yes" : "no", r.overrideThrottles && r.throttleUse ? "yes" : "no",
             helicopter ? fmt::format(", collective {}", r.overridePropPitch ? "yes" : "no") : std::string());
    mRefs.mappedAxes = XPLMFindDataRef("sim/joystick/joy_mapped_axis_value");
}

// How many engines this aircraft has.
int FlightInputs::Engines_cai() const
{
    return std::clamp(mRefs.engines ? XPLMGetDatai(mRefs.engines) : 1, 1, net::kMaxEngines);
}

// Reads an array.
std::vector<float> FlightInputs::ReadArray_cai(XPLMDataRef ref, int count)
{
    std::vector<float> values(static_cast<std::size_t>(count), 0.0f);
    if (ref)
        XPLMGetDatavf(ref, values.data(), 0, count);
    return values;
}

// What my hands and feet do now.
net::FlightInputs FlightInputs::Read_cai() const
{
    const Refs& r = mRefs;
    net::FlightInputs in;
    in.yokePitch = r.yokePitch ? XPLMGetDataf(r.yokePitch) : 0.0f;
    in.yokeRoll = r.yokeRoll ? XPLMGetDataf(r.yokeRoll) : 0.0f;
    in.yokeHeading = r.yokeHeading ? XPLMGetDataf(r.yokeHeading) : 0.0f;
    in.brakeLeft = r.brakeLeft ? XPLMGetDataf(r.brakeLeft) : 0.0f;
    in.brakeRight = r.brakeRight ? XPLMGetDataf(r.brakeRight) : 0.0f;
    in.parkingBrake = r.parking ? XPLMGetDataf(r.parking) : 0.0f;
    in.speedbrake = r.speedbrake ? XPLMGetDataf(r.speedbrake) : 0.0f;
    in.trimElevator = r.trimElevator ? XPLMGetDataf(r.trimElevator) : 0.0f;
    in.trimAileron = r.trimAileron ? XPLMGetDataf(r.trimAileron) : 0.0f;
    in.trimRudder = r.trimRudder ? XPLMGetDataf(r.trimRudder) : 0.0f;
    in.flapRequest = r.flapRequest ? XPLMGetDataf(r.flapRequest) : 0.0f;
    const int engines = Engines_cai();
    in.throttle = ReadArray_cai(r.throttle, engines);
    in.prop = ReadArray_cai(r.prop, engines);
    in.mixture = ReadArray_cai(r.mixture, engines);
    if (r.propMode) {
        std::array<int, net::kMaxEngines> modes{};
        XPLMGetDatavi(r.propMode, modes.data(), 0, engines);
        for (int i = 0; i < engines; ++i)
            in.propMode.push_back(static_cast<std::uint8_t>(std::clamp(modes[static_cast<std::size_t>(i)], 0, 255)));
    }
    return in;
}

// The PIC: sends when changed (30 a second at most) or once a second.
void FlightInputs::Send_cai()
{
    const auto now = Clock::now();
    const auto gap = std::chrono::milliseconds(static_cast<int>(1000.0 / net::kFlightInputsRate));
    if (now - mSentAt < gap)
        return;
    const net::FlightInputs in = Read_cai();
    if (!Differs_cai(in, mSent) && now - mSentAt < std::chrono::seconds(1))
        return;
    mHub.Session_cai().SendFlightInputs_cai(in);
    mSent = in;
    mSentAt = now;
}

// A follower: my own hardware off (true) or back on (false).
void FlightInputs::Overrides_cai(bool on)
{
    if (on == mOverridden)
        return;
    XPLMDataRef refs[4] = {mRefs.overrideJoystick, mRefs.overrideToeBrakes, nullptr,   // throttles: ThrottleOverride_cai
                           mRefs.overridePropPitch};
    for (int i = 0; i < 4; ++i) {
        if (!refs[i])
            continue;
        if (on) {
            mBefore[i] = XPLMGetDatai(refs[i]);
            XPLMSetDatai(refs[i], 1);
        } else {
            XPLMSetDatai(refs[i], mBefore[i]);
        }
    }
    mOverridden = on;
    mHub.Flight_cai().hardwareOff = on;
    LOG_INFO("flight inputs: my own yoke / pedals{} {}", refs[3] ? " / collective" : "",
             on ? "switched off (the PIC flies)" : "back on");
}

// The throttle override: on while somebody else's throttle is written here.
void FlightInputs::ThrottleOverride_cai(bool on)
{
    if (!mRefs.overrideThrottles || !mRefs.throttleUse || on == mThrottleOverridden)
        return;
    if (on) {
        mThrottleBefore = XPLMGetDatai(mRefs.overrideThrottles);
        XPLMSetDatai(mRefs.overrideThrottles, 1);
    } else {
        XPLMSetDatai(mRefs.overrideThrottles, mThrottleBefore);
    }
    mThrottleOverridden = on;
    LOG_INFO("flight inputs: my own throttles {}", on ? "switched off (the PIC or a Crew member works them)" : "back on");
}

// The positions of the levers other Crew members work, over `target`.
void FlightInputs::OverlayOwners_cai(net::FlightInputs& target) const
{
    const net::NetSession& session = mHub.Session_cai();
    for (const auto& [id, in] : session.LeversIn_cai()) {
        const net::Person* owner = session.Find_cai(id);
        if (!owner || owner->pic || id == session.MyId_cai())
            continue;
        const std::uint16_t levers = static_cast<std::uint16_t>(in.levers & owner->levers);   // what they still have
        const net::FlightInputs& f = in.inputs;
        if (levers & net::kLeverThrottle) { target.throttle = f.throttle; target.propMode = f.propMode; }
        if (levers & net::kLeverProp) target.prop = f.prop;
        if (levers & net::kLeverMixture) target.mixture = f.mixture;
        if (levers & net::kLeverFlaps) target.flapRequest = f.flapRequest;
        if (levers & net::kLeverSpeedbrake) target.speedbrake = f.speedbrake;
        if (levers & net::kLeverTrims) {
            target.trimElevator = f.trimElevator; target.trimAileron = f.trimAileron; target.trimRudder = f.trimRudder;
        }
        if (levers & net::kLeverParking) target.parkingBrake = f.parkingBrake;
    }
}

// A lever owner: mine to everybody when changed (30 a second at most) or once a second.
void FlightInputs::SendLevers_cai()
{
    const auto now = Clock::now();
    if (now - mLeversSentAt < std::chrono::milliseconds(static_cast<int>(1000.0 / net::kFlightInputsRate)))
        return;
    const net::FlightInputs in = Read_cai();
    if (!Differs_cai(in, mLeversSent) && now - mLeversSentAt < std::chrono::seconds(1))
        return;
    mHub.Session_cai().SendLevers_cai(in);
    mLeversSent = in;
    mLeversSentAt = now;
}

// A follower: writes the PIC's inputs.
void FlightInputs::Write_cai(const net::FlightInputs& in, std::uint16_t what)
{
    const Refs& r = mRefs;
    if (what & kWriteHands) {
        if (r.yokePitch) XPLMSetDataf(r.yokePitch, in.yokePitch);
        if (r.yokeRoll) XPLMSetDataf(r.yokeRoll, in.yokeRoll);
        if (r.yokeHeading) XPLMSetDataf(r.yokeHeading, in.yokeHeading);
        if (r.brakeLeft) XPLMSetDataf(r.brakeLeft, in.brakeLeft);
        if (r.brakeRight) XPLMSetDataf(r.brakeRight, in.brakeRight);
    }
    if (r.parking && (what & net::kLeverParking)) XPLMSetDataf(r.parking, in.parkingBrake);
    if (r.speedbrake && (what & net::kLeverSpeedbrake)) XPLMSetDataf(r.speedbrake, in.speedbrake);
    if (r.flapRequest && (what & net::kLeverFlaps)) XPLMSetDataf(r.flapRequest, in.flapRequest);
    // Trims: a hardware trim wheel moving them back is counted like a lever.
    XPLMDataRef trims[3] = {r.trimElevator, r.trimAileron, r.trimRudder};
    const float trimValues[3] = {in.trimElevator, in.trimAileron, in.trimRudder};
    bool trimMoved = false;
    for (int k = 0; k < 3; ++k) {
        if (!trims[k] || !(what & net::kLeverTrims))
            continue;
        trimMoved = trimMoved || (mTrimWritten && std::fabs(XPLMGetDataf(trims[k]) - mTrim[k]) > kHeldTolerance);
        XPLMSetDataf(trims[k], trimValues[k]);
        mTrim[k] = trimValues[k];
    }
    mTrimWritten = true;
    mHeld += trimMoved ? 1 : 0;
    if (r.propMode && !in.propMode.empty() && (what & net::kLeverThrottle)) {
        // Reverse / beta: only when it differs (writing the mode is a request, not every frame).
        const int n = std::min(Engines_cai(), static_cast<int>(in.propMode.size()));
        std::array<int, net::kMaxEngines> now{};
        XPLMGetDatavi(r.propMode, now.data(), 0, n);
        for (int i = 0; i < n; ++i) {
            int mode = in.propMode[static_cast<std::size_t>(i)];
            if (now[static_cast<std::size_t>(i)] != mode)
                XPLMSetDatavi(r.propMode, &mode, i, 1);
        }
    }
    const int engines = Engines_cai();
    // Count the frames where my hardware moved a lever away from what was written last frame.
    XPLMDataRef levers[3] = {r.throttle, r.prop, r.mixture};
    const std::vector<float>* values[3] = {&in.throttle, &in.prop, &in.mixture};
    const std::uint16_t bits[3] = {net::kLeverThrottle, net::kLeverProp, net::kLeverMixture};
    for (int k = 0; k < 3; ++k) {
        if (!levers[k] || values[k]->empty() || !(what & bits[k])) {
            mWritten[k].clear();
            continue;
        }
        const std::vector<float> now = ReadArray_cai(levers[k], engines);
        bool moved = false;
        for (std::size_t i = 0; i < mWritten[k].size() && i < now.size(); ++i)
            moved = moved || std::fabs(now[i] - mWritten[k][i]) > kHeldTolerance;
        mHeld += moved ? 1 : 0;
        const int n = std::min(engines, static_cast<int>(values[k]->size()));
        std::vector<float> out = *values[k];
        XPLMSetDatavf(levers[k], out.data(), 0, n);
        mWritten[k].assign(out.begin(), out.begin() + n);
    }
    if (r.throttleUse && mThrottleOverridden && !in.throttle.empty() && (what & net::kLeverThrottle)) {
        std::vector<float> out = in.throttle;
        XPLMSetDatavf(r.throttleUse, out.data(), 0, std::min(engines, static_cast<int>(out.size())));
    }
}

// The copilot side's overrides on or off.
void FlightInputs::CopilotOverrides_cai(bool on)
{
    if (on == mCopOverridden)
        return;
    for (int i = 0; i < 3; ++i) {
        if (!mRefs.copOverride[i])
            continue;
        if (on) {
            mCopBefore[i] = XPLMGetDatai(mRefs.copOverride[i]);
            XPLMSetDatai(mRefs.copOverride[i], 1);
        } else {
            XPLMSetDatai(mRefs.copOverride[i], mCopBefore[i]);
        }
    }
    mCopOverridden = on;
    LOG_INFO("copilot axes: the copilot side {}", on ? "comes from the session's copilot" : "is X-Plane's own again");
}

// Copilot axes: sends mine (the copilot) or writes the copilot's (everybody else).
void FlightInputs::TickCopilot_cai(bool iAmPic)
{
    net::NetSession& session = mHub.Session_cai();
    net::FlightStats& stats = mHub.Flight_cai();
    const Refs& r = mRefs;
    const bool available = r.copPitch && r.copRoll && r.copHeading && r.copOverride[0] && r.copOverride[1] && r.copOverride[2];
    stats.copilotAvailable = available;
    auto in = session.TakeCopilotInputs_cai();
    if (!session.Rules_cai().copilotAxes || !available) {
        CopilotOverrides_cai(false);
        mCopLast.reset();
        stats.iAmCopilot = false;
        stats.copilotId = net::kNoId;
        return;
    }
    const auto now = Clock::now();
    if (session.MayCopilot_cai() && !iAmPic) {
        // I am (or may be) the copilot: my own hardware on my copilot side, sent to everybody.
        CopilotOverrides_cai(false);
        stats.iAmCopilot = true;
        const net::CopilotInputs mine{XPLMGetDataf(r.copPitch), XPLMGetDataf(r.copRoll), XPLMGetDataf(r.copHeading)};
        const auto gap = std::chrono::milliseconds(static_cast<int>(1000.0 / net::kFlightInputsRate));
        const bool changed = std::fabs(mine.pitch - mCopSent.pitch) > kChange || std::fabs(mine.roll - mCopSent.roll) > kChange ||
                             std::fabs(mine.heading - mCopSent.heading) > kChange;
        if (now - mCopSentAt >= gap && (changed || now - mCopSentAt >= std::chrono::milliseconds(250))) {
            session.SendCopilotInputs_cai(mine);              // every 250 ms when still: the others' 0.5 s stale limit
            mCopSent = mine;
            mCopSentAt = now;
        }
        return;
    }
    // Everybody else: the copilot side is the session copilot's.
    stats.iAmCopilot = false;
    if (in) {
        mCopLast = *in;
        mCopAt = now;
    }
    CopilotOverrides_cai(true);
    const bool fresh = mCopLast && std::chrono::duration<double>(now - mCopAt).count() <= net::kCopilotStaleS;
    stats.copilotId = fresh ? mCopLast->id : net::kNoId;
    const net::CopilotInputs value = fresh ? mCopLast->inputs : net::CopilotInputs{};   // stale or none: centred
    XPLMSetDataf(r.copPitch, value.pitch);
    XPLMSetDataf(r.copRoll, value.roll);
    XPLMSetDataf(r.copHeading, value.heading);
}

// I became the PIC or stopped being it.
void FlightInputs::OnPicChanged_cai(bool iAmPic)
{
    for (auto& written : mWritten)
        written.clear();
    mTrimWritten = false;
    if (mBlend.step != Blend::Step::None)
        EndBlend_cai("the PIC changed again");
    if (iAmPic) {
        const double blendS = mHub.Session_cai().Rules_cai().handoverBlendMs / 1000.0;
        if (mLast && mOverridden && blendS > 0.0 && mRefs.mappedAxes && mRefs.overrideJoystick && mRefs.yokePitch) {
            // A handover: from the old PIC's yoke and rudder to my hardware (TickBlend_cai).
            mBlend = Blend{};
            mBlend.step = Blend::Step::Probe;
            mBlend.from[0] = mLast->yokePitch;
            mBlend.from[1] = mLast->yokeRoll;
            mBlend.from[2] = mLast->yokeHeading;
            mBlend.seconds = blendS;
        }
        Overrides_cai(false);                         // mine again; the blend puts the joystick override back on at once
        mLast.reset();
        mSent = {};
        mSentAt = {};
        LOG_INFO("flight inputs: I am the PIC - my yoke, pedals and levers go to everybody");
    } else {
        LOG_INFO("flight inputs: following the PIC's yoke, pedals and levers");
    }
}

// Every frame.
void FlightInputs::Tick_cai(bool iAmPic)
{
    FindRefs_cai();
    net::FlightStats& stats = mHub.Flight_cai();
    stats.engines = static_cast<std::size_t>(Engines_cai());
    TickCopilot_cai(iAmPic);
    const net::NetSession& session = mHub.Session_cai();
    const std::uint16_t mine = session.MyLevers_cai();      // levers my hands work (Crew)
    const std::uint16_t others = session.OthersLevers_cai(); // levers other Crew members work
    if (iAmPic) {
        TickBlend_cai();
        Send_cai();
        // Levers a Crew member works: written here from them (my hardware's are held).
        if (others != 0) {
            net::FlightInputs target = Read_cai();
            OverlayOwners_cai(target);
            Write_cai(target, others);
        }
        ThrottleOverride_cai((others & net::kLeverThrottle) != 0);
        return;
    }
    if (mine != 0)
        SendLevers_cai();
    if (auto in = mHub.Session_cai().TakeFlightInputs_cai()) {
        mLast = std::move(*in);
        stats.inputsAt = Clock::now();
    }
    if (!mLast)
        return;                                       // nothing from the PIC yet: my hardware stays mine
    Overrides_cai(true);
    ThrottleOverride_cai((mine & net::kLeverThrottle) == 0);
    net::FlightInputs target = *mLast;
    OverlayOwners_cai(target);
    Write_cai(target, static_cast<std::uint16_t>(kWriteHands | (net::kLeverAll & ~mine)));
    const auto now = Clock::now();
    if (now - mHeldSince >= std::chrono::seconds(1)) {
        if (mHeld > 0 && stats.heldPerSecond == 0)
            LOG_INFO("flight inputs: my hardware moves a lever away from the PIC's ({} frames a second) - it is held", mHeld);
        stats.heldPerSecond = mHeld;
        mHeld = 0;
        mHeldSince = now;
    }
}

// The PIC: one step of the handover blend.
void FlightInputs::TickBlend_cai()
{
    if (mBlend.step == Blend::Step::None)
        return;
    const Refs& r = mRefs;
    XPLMDataRef yoke[3] = {r.yokePitch, r.yokeRoll, r.yokeHeading};
    float raw[4] = {0.0f, 0.0f, 0.0f, 0.0f};                     // index 0 unused: 1 pitch, 2 roll, 3 yaw
    XPLMGetDatavf(r.mappedAxes, raw, 0, 4);
    if (mBlend.step == Blend::Step::Check) {
        if (mBlend.checkFrames-- > 0)
            return;                                   // X-Plane writes my hardware into the yoke this frame
        std::string axes;
        for (int k = 0; k < 3; ++k)
            if (yoke[k])
                axes += fmt::format("{}{} {:+.2f} (blend {:+.2f})", k ? ", " : "", k == 0 ? "pitch" : k == 1 ? "roll" : "rudder",
                                    XPLMGetDataf(yoke[k]), mBlend.target[k]);
        LOG_INFO("flight inputs: handover check - my yoke and rudder now: {}", axes);
        mBlend.step = Blend::Step::None;
        return;
    }
    if (mBlend.step == Blend::Step::Probe) {
        // The raw axes are my hardware's position (-1..1, like yoke_*_ratio; 0.9.1 tests showed it for pitch and rudder).
        XPLMSetDatai(r.overrideJoystick, 1);          // the plugin writes the yoke until the blend ends
        std::string axes;
        for (int k = 0; k < 3; ++k)
            axes += fmt::format("{}{} {:+.2f} -> {:+.2f}", k ? ", " : "", k == 0 ? "pitch" : k == 1 ? "roll" : "rudder", mBlend.from[k],
                                std::clamp(raw[k + 1], -1.0f, 1.0f));
        LOG_INFO("flight inputs: handover - blending to my yoke and rudder over {:.1f} s: {}", mBlend.seconds, axes);
        mBlend.step = Blend::Step::Blending;
        mBlend.start = Clock::now();
    }
    const double t = std::chrono::duration<double>(Clock::now() - mBlend.start).count() / mBlend.seconds;
    if (t >= 1.0) {
        for (int k = 0; k < 3; ++k)
            mBlend.target[k] = std::clamp(raw[k + 1], -1.0f, 1.0f);
        EndBlend_cai("done");
        mBlend.step = Blend::Step::Check;             // a log line: did the blend end where my hardware is?
        mBlend.checkFrames = 1;
        return;
    }
    const float a = static_cast<float>(t * t * (3.0 - 2.0 * t));   // smoothstep: no kink at either end
    for (int k = 0; k < 3; ++k) {
        if (!yoke[k])
            continue;
        const float mine = raw[k + 1];
        XPLMSetDataf(yoke[k], mBlend.from[k] + (std::clamp(mine, -1.0f, 1.0f) - mBlend.from[k]) * a);
    }
}

// Ends the handover blend: my joystick is mine.
void FlightInputs::EndBlend_cai(const char* why)
{
    if (mBlend.step == Blend::Step::None || mBlend.step == Blend::Step::Check) {
        mBlend.step = Blend::Step::None;
        return;
    }
    if (mBlend.step == Blend::Step::Blending && mRefs.overrideJoystick)
        XPLMSetDatai(mRefs.overrideJoystick, mBefore[0]);   // as it was before I followed
    mBlend.step = Blend::Step::None;
    LOG_INFO("flight inputs: handover blend {} - my yoke and rudder fly", why);
}

// Gives everything back to X-Plane.
void FlightInputs::Stop_cai()
{
    EndBlend_cai("stopped");
    Overrides_cai(false);
    ThrottleOverride_cai(false);
    CopilotOverrides_cai(false);
    mCopLast.reset();
    mLast.reset();
    mHub.Flight_cai().heldPerSecond = 0;
}

} // namespace sc::sync
