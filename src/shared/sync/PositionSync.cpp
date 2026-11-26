// shared_cockpit - where everybody's aircraft is, and the move to the PIC (0.6)

#include "shared/sync/PositionSync.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

#include "fmt/format.h"
#include "XPLMGraphics.h"
#include "XPLMPlanes.h"
#include "XPLMUtilities.h"

#include "shared/log/Log.h"
#include "shared/util/Utils.h"
#include "shared/xp/XPlane.h"

namespace sc::sync {

namespace {

constexpr double kRad          = 3.14159265358979323846 / 180.0;
constexpr double kNearM        = net::kFollowMaxM;   // farther: a new flight instead of a direct move
constexpr double kHoldS        = 2.0;       // the flight model stays off this long after placing (the target written each frame)
constexpr double kSettleS      = 3.0;       // after the flight model is back: watch the height and the g-load, then measure
constexpr double kAfterLoadS   = 3.0;       // after the reload: let the aircraft start, then ask the snapshot
constexpr double kNoLoadS      = 10.0;      // no reload message after this: go on anyway
constexpr double kReloadMaxS   = 45.0;      // the whole reload step at most
constexpr double kPicMaxAgeS   = 5.0;       // an older PIC position is not used

// X-Plane 12.4's XPLMInitFlight, found at run time (older X-Plane versions load the plugin too).
using InitFlightFn = int (*)(const char*);

// Seconds since `at`.
double Since_cai(std::chrono::steady_clock::time_point at)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - at).count();
}

} // namespace

PositionSync::PositionSync(net::SessionHub& hub, const settings::Settings& settings) : mHub(hub), mSettings(settings)
{
}

PositionSync::~PositionSync()
{
    if (mHub.Position_cai().move == net::PositionStats::Move::Moving && mRefs.overridePath)
        XPLMSetDatavi(mRefs.overridePath, &mOverrideBefore, 0, 1);   // never leave the flight model off
    mHub.Position_cai().hold = false;
    if (mProbe)
        XPLMDestroyProbe(mProbe);
}

// Finds the datarefs (once).
void PositionSync::FindRefs_cai()
{
    if (mFound)
        return;
    mFound = true;
    auto lmbda_find = [](const char* name) {
        XPLMDataRef ref = XPLMFindDataRef(name);
        if (!ref)
            LOG_WARN("position: dataref {} not found", name);
        return ref;
    };
    Refs& r = mRefs;
    r.lat = lmbda_find("sim/flightmodel/position/latitude");
    r.lon = lmbda_find("sim/flightmodel/position/longitude");
    r.elev = lmbda_find("sim/flightmodel/position/elevation");
    r.agl = lmbda_find("sim/flightmodel/position/y_agl");
    r.x = lmbda_find("sim/flightmodel/position/local_x");
    r.y = lmbda_find("sim/flightmodel/position/local_y");
    r.z = lmbda_find("sim/flightmodel/position/local_z");
    r.psi = lmbda_find("sim/flightmodel/position/psi");
    r.truePsi = lmbda_find("sim/flightmodel/position/true_psi");
    r.theta = lmbda_find("sim/flightmodel/position/theta");
    r.phi = lmbda_find("sim/flightmodel/position/phi");
    r.q = lmbda_find("sim/flightmodel/position/q");
    r.vx = lmbda_find("sim/flightmodel/position/local_vx");
    r.vy = lmbda_find("sim/flightmodel/position/local_vy");
    r.vz = lmbda_find("sim/flightmodel/position/local_vz");
    r.p = lmbda_find("sim/flightmodel/position/P");
    r.qRate = lmbda_find("sim/flightmodel/position/Q");
    r.r = lmbda_find("sim/flightmodel/position/R");
    r.groundSpeed = lmbda_find("sim/flightmodel/position/groundspeed");
    r.onGround = lmbda_find("sim/flightmodel/failures/onground_any");
    r.running = lmbda_find("sim/flightmodel/engine/ENGN_running");
    r.engineCount = lmbda_find("sim/aircraft/engine/acf_num_engines");
    r.overridePath = lmbda_find("sim/operation/override/override_planepath");
    r.eqlbm = lmbda_find("sim/aircraft/gear/acf_h_eqlbm");
    r.gNormal = lmbda_find("sim/flightmodel/forces/g_nrml");
    mProbe = XPLMCreateProbe(xplm_ProbeY);
}

// My position now.
net::PositionInfo PositionSync::ReadMine_cai() const
{
    const Refs& r = mRefs;
    net::PositionInfo p;
    p.latitude = r.lat ? XPLMGetDatad(r.lat) : 0.0;
    p.longitude = r.lon ? XPLMGetDatad(r.lon) : 0.0;
    p.elevation = r.elev ? XPLMGetDatad(r.elev) : 0.0;
    p.agl = r.agl ? XPLMGetDataf(r.agl) : 0.0f;
    p.heading = r.truePsi ? XPLMGetDataf(r.truePsi) : (r.psi ? XPLMGetDataf(r.psi) : 0.0f);
    p.pitch = r.theta ? XPLMGetDataf(r.theta) : 0.0f;
    p.roll = r.phi ? XPLMGetDataf(r.phi) : 0.0f;
    p.groundSpeed = r.groundSpeed ? XPLMGetDataf(r.groundSpeed) : 0.0f;
    p.onGround = r.onGround ? XPLMGetDatai(r.onGround) != 0 : true;
    const int count = std::clamp(r.engineCount ? XPLMGetDatai(r.engineCount) : 0, 0, 16);
    p.engineCount = static_cast<std::uint8_t>(count);
    if (r.running && count > 0) {
        std::array<int, 16> running{};
        XPLMGetDatavi(r.running, running.data(), 0, count);
        auto lmbda_running = [](int v) { return v != 0; };
        p.engines = static_cast<std::uint8_t>(std::count_if(running.begin(), running.begin() + count, lmbda_running));
    }
    return p;
}

// The PIC's last position.
std::optional<net::NetSession::KnownPosition> PositionSync::PicPosition_cai() const
{
    const net::NetSession& session = mHub.Session_cai();
    const std::uint8_t pic = session.PicId_cai();
    if (pic == session.MyId_cai())
        return std::nullopt;
    const auto found = session.Positions_cai().find(pic);
    if (found == session.Positions_cai().end() || Since_cai(found->second.at) > kPicMaxAgeS)
        return std::nullopt;
    return found->second;
}

// Compares mine with the PIC's.
void PositionSync::Compare_cai(const net::PositionInfo& mine)
{
    net::NetSession& session = mHub.Session_cai();
    net::PositionStats& stats = mHub.Position_cai();
    stats.iAmPic = session.PicId_cai() == session.MyId_cai();
    stats.picName = session.NameOf_cai(session.PicId_cai());
    const auto pic = PicPosition_cai();
    stats.known = pic.has_value();
    if (!pic) {
        stats.off = false;
        stats.ask = stats.ask && !stats.iAmPic;
        return;
    }
    const net::PositionInfo& p = pic->position;
    const double flat = Utils::DistanceM_cai(mine.latitude, mine.longitude, p.latitude, p.longitude);
    stats.picOnGround = p.onGround;
    stats.distanceM = p.onGround ? flat : std::hypot(flat, mine.elevation - p.elevation);
    stats.headingOff = static_cast<float>(Utils::HeadingDiff_cai(mine.heading, p.heading));
    stats.picEngines = p.engines > 0;
    stats.enginesDiffer = (p.engines > 0) != (mine.engines > 0);
    const net::SessionRules& limits = session.Rules_cai();
    if (limits.flight == net::FlightMode::Follow)
        stats.off = flat > net::kFollowMaxM;          // Follow closes anything nearer by itself
    else
        stats.off = p.onGround ? (stats.distanceM > limits.groundM || stats.headingOff > limits.headingDeg || stats.enginesDiffer)
                               : stats.distanceM > limits.airM;
    if (!mAsked && stats.move == net::PositionStats::Move::None) {
        mAsked = true;                              // once per session: the first comparison after joining
        stats.ask = stats.off && limits.askOnJoin;
        if (stats.off)
            LOG_INFO("position: {} from the PIC '{}' (heading {:.0f}° off{}): asking to move", Utils::DistanceText_cai(stats.distanceM),
                     stats.picName, stats.headingOff, stats.enginesDiffer ? ", engines differ" : "");
    }
}

// My aircraft's centre above the ground when resting on its gear: acf_h_eqlbm; without it, measured
// now (my centre minus the ground under me) if I stand on the ground; the PIC's only as the last resort.
double PositionSync::OwnHeightOnGear_cai(double picAgl)
{
    const float eqlbm = mRefs.eqlbm ? XPLMGetDataf(mRefs.eqlbm) : 0.0f;
    if (eqlbm > 0.1f)
        return eqlbm;
    if (mProbe && mRefs.x && mRefs.y && mRefs.z && mRefs.onGround && XPLMGetDatai(mRefs.onGround) != 0) {
        XPLMProbeInfo_t info;
        info.structSize = sizeof(info);
        const double x = XPLMGetDatad(mRefs.x), y = XPLMGetDatad(mRefs.y), z = XPLMGetDatad(mRefs.z);
        if (XPLMProbeTerrainXYZ(mProbe, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), &info) == xplm_ProbeHitTerrain) {
            LOG_INFO("position: acf_h_eqlbm missing - my height on the gear measured: {:.2f} m", y - info.locationY);
            return y - info.locationY;
        }
    }
    LOG_WARN("position: my height on the gear is unknown - using the PIC's ({:.2f} m)", picAgl);
    return picAgl;
}

// The direct move's target in local coordinates.
bool PositionSync::MakeTarget_cai(const net::PositionInfo& pic, double ageSeconds, Target& target)
{
    target.pos = pic;
    double lat = pic.latitude, lon = pic.longitude;
    if (!pic.onGround && pic.groundSpeed > 1.0f) {
        // In the air the PIC moved on since its last position: one step ahead along its heading.
        const double ahead = pic.groundSpeed * ageSeconds;
        lat += ahead * std::cos(pic.heading * kRad) / 111320.0;
        lon += ahead * std::sin(pic.heading * kRad) / (111320.0 * std::max(0.01, std::cos(lat * kRad)));
    }
    XPLMWorldToLocal(lat, lon, pic.elevation, &target.x, &target.y, &target.z);
    if (pic.onGround && mProbe) {
        // On the ground: the terrain here under that point, plus my own aircraft's height on its gear.
        XPLMProbeInfo_t info;
        info.structSize = sizeof(info);
        if (XPLMProbeTerrainXYZ(mProbe, static_cast<float>(target.x), static_cast<float>(target.y), static_cast<float>(target.z),
                                &info) == xplm_ProbeHitTerrain)
        {
            // Always my own aircraft's height: the PIC's aircraft should be the same, but may not be.
            target.y = info.locationY + OwnHeightOnGear_cai(pic.agl);
            target.terrainY = info.locationY;
        } else
            LOG_WARN("position: no terrain under the PIC's position here (scenery not loaded?) - using its elevation");
    }
    target.vx = target.vy = target.vz = 0.0f;
    if (!pic.onGround) {
        target.vx = static_cast<float>(pic.groundSpeed * std::sin(pic.heading * kRad));
        target.vz = static_cast<float>(-pic.groundSpeed * std::cos(pic.heading * kRad));
    }
    return mRefs.x && mRefs.y && mRefs.z && mRefs.psi && mRefs.theta && mRefs.phi;
}

// Writes the target with the flight model off.
void PositionSync::WriteTarget_cai(const Target& target)
{
    const Refs& r = mRefs;
    XPLMSetDatad(r.x, target.x);
    XPLMSetDatad(r.y, target.y);
    XPLMSetDatad(r.z, target.z);
    // The local frame's north is true north near the reference point: the true heading is close enough.
    XPLMSetDataf(r.psi, target.pos.heading);
    XPLMSetDataf(r.theta, target.pos.pitch);
    XPLMSetDataf(r.phi, target.pos.roll);
    if (r.q) {
        const double psi = target.pos.heading * kRad / 2.0, theta = target.pos.pitch * kRad / 2.0, phi = target.pos.roll * kRad / 2.0;
        float q[4];
        q[0] = static_cast<float>(std::cos(psi) * std::cos(theta) * std::cos(phi) + std::sin(psi) * std::sin(theta) * std::sin(phi));
        q[1] = static_cast<float>(std::cos(psi) * std::cos(theta) * std::sin(phi) - std::sin(psi) * std::sin(theta) * std::cos(phi));
        q[2] = static_cast<float>(std::cos(psi) * std::sin(theta) * std::cos(phi) + std::sin(psi) * std::cos(theta) * std::sin(phi));
        q[3] = static_cast<float>(-std::cos(psi) * std::sin(theta) * std::sin(phi) + std::sin(psi) * std::cos(theta) * std::cos(phi));
        XPLMSetDatavf(r.q, q, 0, 4);
    }
    if (r.vx) XPLMSetDataf(r.vx, target.vx);
    if (r.vy) XPLMSetDataf(r.vy, target.vy);
    if (r.vz) XPLMSetDataf(r.vz, target.vz);
    if (r.p) XPLMSetDataf(r.p, 0.0f);
    if (r.qRate) XPLMSetDataf(r.qRate, 0.0f);
    if (r.r) XPLMSetDataf(r.r, 0.0f);
}

// A new flight at the PIC's position.
bool PositionSync::InitFlight_cai(const net::PositionInfo& pic, std::string& why)
{
    auto initFlight = reinterpret_cast<InitFlightFn>(XPLMFindSymbol("XPLMInitFlight"));
    const auto aircraft = xp::GetUserAircraft_cai();
    if (!aircraft) { why = "X-Plane does not report the aircraft's file"; return false; }
    if (!initFlight) {
        if (!pic.onGround) {
            // Older X-Plane: an in-air start (engines always run there anyway).
            LOG_INFO("position: XPLMInitFlight not found (X-Plane before 12.4) - XPLMPlaceUserAtLocation in the air");
            XPLMPlaceUserAtLocation(pic.latitude, pic.longitude, static_cast<float>(pic.elevation), pic.heading, pic.groundSpeed);
            return true;
        }
        why = "a move this far needs X-Plane 12.4 or newer: taxi closer, or start at the PIC's airport by hand";
        return false;
    }
    std::error_code error;
    std::filesystem::path path = std::filesystem::relative(aircraft->acfFile, xp::GetXPlaneFolder_cai(), error);
    if (error || path.empty() || path.native().starts_with(std::filesystem::path("..").native()))
        path = aircraft->acfFile;
    std::string pathText = Utils::Utf8_cai(path);
    std::replace(pathText.begin(), pathText.end(), '\\', '/');
    std::string escaped;
    for (char c : pathText) {
        if (c == '"' || c == '\\')
            escaped += '\\';
        escaped += c;
    }
    const std::string start =
        pic.onGround ? fmt::format(R"("lle_ground_start":{{"latitude":{:.8f},"longitude":{:.8f},"heading_true":{:.2f}}})",
                                   pic.latitude, pic.longitude, std::fmod(pic.heading + 360.0f, 360.0f))
                     : fmt::format(R"("lle_air_start":{{"latitude":{:.8f},"longitude":{:.8f},"elevation_in_meters":{:.1f},)"
                                   R"("heading_true":{:.2f},"speed_in_meters_per_second":{:.1f},"pitch_in_degrees":{:.1f}}})",
                                   pic.latitude, pic.longitude, pic.elevation, std::fmod(pic.heading + 360.0f, 360.0f),
                                   std::max(pic.groundSpeed, 30.0f), pic.pitch);
    const bool running = pic.engines > 0 || !pic.onGround;
    const std::string json = fmt::format(R"({{"aircraft":{{"path":"{}"}},{},"engine_status":{{"all_engines":{{"running":{}}}}}}})",
                                         escaped, start, running ? "true" : "false");
    LOG_INFO("position: new flight at the PIC's position: {}", json);
    const int result = initFlight(json.c_str());
    if (result != 0) {
        static constexpr const char* kResults[] = {"ok", "invalid flight description", "aircraft not found", "livery not found",
                                                   "airport not found", "ramp not found", "runway not found"};
        why = fmt::format("X-Plane did not start the flight: {}", result >= 0 && result <= 6 ? kResults[result] : "unknown error");
        return false;
    }
    return true;
}

// Starts a move to the PIC.
void PositionSync::StartMove_cai()
{
    net::PositionStats& stats = mHub.Position_cai();
    stats.ask = false;
    const auto pic = PicPosition_cai();
    if (!pic) {
        mHub.Note_cai(net::SessionEvent::Kind::Declined, "Move: the PIC's position is not known (yet)");
        return;
    }
    const net::PositionInfo mine = ReadMine_cai();
    const double distance = Utils::DistanceM_cai(mine.latitude, mine.longitude, pic->position.latitude, pic->position.longitude);
    const bool enginesDiffer = (pic->position.engines > 0) != (mine.engines > 0);
    mFar = distance > kNearM || enginesDiffer;
    mStepAt = Clock::now();
    if (!mFar) {
        if (!MakeTarget_cai(pic->position, Since_cai(pic->at), mTarget)) {
            mHub.Note_cai(net::SessionEvent::Kind::Declined, "Move: X-Plane's position datarefs are missing");
            return;
        }
        LOG_INFO("position: direct move to the PIC '{}' ({} away, heading {:.0f}° -> {:.0f}°)", stats.picName,
                 Utils::DistanceText_cai(distance), mine.heading, pic->position.heading);
        LOG_INFO("position: target ground {}, on the gear {:.2f} m (the PIC's height above ground {:.2f} m), {}",
                 mTarget.terrainY ? fmt::format("{:.2f} (local y)", *mTarget.terrainY) : std::string("not probed"),
                 mRefs.eqlbm ? XPLMGetDataf(mRefs.eqlbm) : 0.0f, pic->position.agl, pic->position.onGround ? "on the ground" : "in the air");
        if (mRefs.overridePath)
            XPLMGetDatavi(mRefs.overridePath, &mOverrideBefore, 0, 1);
        mFrames = 0;
        mStepAt = Clock::now();
        stats.move = net::PositionStats::Move::Moving;
        return;
    }
    LOG_INFO("position: {} from the PIC{} - starting a new flight there", Utils::DistanceText_cai(distance),
             enginesDiffer ? fmt::format(" and its engines {}", pic->position.engines > 0 ? "run" : "are off") : std::string());
    stats.hold = true;                               // the reload's own start-up is not shared
    mLoadedAt.reset();
    mSnapshotAsked = false;
    stats.move = net::PositionStats::Move::Reloading;
    std::string why;
    if (!InitFlight_cai(pic->position, why)) {
        stats.hold = false;
        Finish_cai(net::SessionEvent::Kind::Warning, fmt::format("Move: {}", why));
        return;
    }
    mHub.Note_cai(net::SessionEvent::Kind::Notice, fmt::format("Moving to {}'s position (new flight)...", stats.picName));
}

// The steps of a move.
void PositionSync::StepMove_cai()
{
    net::PositionStats& stats = mHub.Position_cai();
    using Move = net::PositionStats::Move;
    switch (stats.move) {
        case Move::None:
            return;
        case Move::Moving: {
            // Direct move: the flight model off, the target written every frame.
            int off = 1;
            if (mRefs.overridePath)
                XPLMSetDatavi(mRefs.overridePath, &off, 0, 1);
            if (mTarget.terrainY && mProbe) {
                // The scenery at the new place may still be paging in: probe again while holding.
                XPLMProbeInfo_t info;
                info.structSize = sizeof(info);
                if (XPLMProbeTerrainXYZ(mProbe, static_cast<float>(mTarget.x), static_cast<float>(mTarget.y), static_cast<float>(mTarget.z),
                                        &info) == xplm_ProbeHitTerrain && std::fabs(info.locationY - *mTarget.terrainY) > 0.05) {
                    LOG_INFO("position: the ground under the target moved {:.2f} m (scenery paged in)", info.locationY - *mTarget.terrainY);
                    mTarget.y += info.locationY - *mTarget.terrainY;
                    mTarget.terrainY = info.locationY;
                }
            }
            WriteTarget_cai(mTarget);
            ++mFrames;
            if (Since_cai(mStepAt) < kHoldS)
                return;
            if (mRefs.overridePath)
                XPLMSetDatavi(mRefs.overridePath, &mOverrideBefore, 0, 1);
            WriteTarget_cai(mTarget);                // the speeds again, with the flight model back on
            LOG_INFO("position: placed, flight model off for {} frames ({:.1f} s), now back on", mFrames, kHoldS);
            stats.move = Move::Settling;
            mStepAt = Clock::now();
            mMinAgl = 1e9f;
            mMaxG = 0.0f;
            return;
        }
        case Move::Reloading: {
            if (!mLoadedAt && Since_cai(mStepAt) > kNoLoadS) {
                LOG_INFO("position: no load message from X-Plane after {:.0f} s - going on", kNoLoadS);
                mLoadedAt = Clock::now();
            }
            if (mLoadedAt && Since_cai(*mLoadedAt) > kAfterLoadS) {
                std::string why;
                mSnapshotAsked = mHub.Session_cai().RequestSnapshot_cai(why);
                if (!mSnapshotAsked)
                    LOG_INFO("position: no snapshot after the reload ({})", why);
                stats.move = Move::Settling;
                mStepAt = Clock::now();
            } else if (Since_cai(mStepAt) > kReloadMaxS) {
                stats.hold = false;
                Finish_cai(net::SessionEvent::Kind::Warning, "Move: the new flight did not finish loading");
            }
            return;
        }
        case Move::Settling: {
            if (mFar) {
                // Wait for the snapshot written after the reload (or give up after a while).
                const bool written = !mSnapshotAsked || stats.snapshotAt > mStepAt;
                if (!written && Since_cai(mStepAt) < kReloadMaxS)
                    return;
                if (Since_cai(written ? std::max(stats.snapshotAt, mStepAt) : mStepAt) < 1.0)
                    return;
                stats.hold = false;
                if (!written)
                    LOG_WARN("position: the snapshot after the reload did not come - the cockpit may differ");
            } else if (Since_cai(mStepAt) < kSettleS) {
                // Watch the first seconds with the flight model back: a drop or a hard g-load shows here.
                if (mRefs.agl)
                    mMinAgl = std::min(mMinAgl, XPLMGetDataf(mRefs.agl));
                if (mRefs.gNormal)
                    mMaxG = std::max(mMaxG, std::fabs(XPLMGetDataf(mRefs.gNormal)));
                return;
            }
            if (!mFar)
                LOG_INFO("position: after the move: lowest height above ground {:.2f} m (on the gear {:.2f} m), highest g-load {:.2f}",
                         mMinAgl, mRefs.eqlbm ? XPLMGetDataf(mRefs.eqlbm) : 0.0f, mMaxG);
            const auto pic = PicPosition_cai();
            const net::PositionInfo mine = ReadMine_cai();
            if (!pic) {
                Finish_cai(net::SessionEvent::Kind::Notice, "Moved to the PIC's last position");
                return;
            }
            const double distance = Utils::DistanceM_cai(mine.latitude, mine.longitude, pic->position.latitude, pic->position.longitude);
            const double heading = Utils::HeadingDiff_cai(mine.heading, pic->position.heading);
            const net::SessionRules& rules = mHub.Session_cai().Rules_cai();
            const bool good = pic->position.onGround ? distance <= rules.groundM : distance <= rules.airM;
            Finish_cai(good ? net::SessionEvent::Kind::Notice : net::SessionEvent::Kind::Warning,
                       fmt::format("Moved to {}'s position: {} away, heading {:.1f}° off{}{}", stats.picName,
                                   Utils::DistanceText_cai(distance), heading, mFar ? " (new flight, cockpit set from the session)" : "",
                                   good ? "" : " - still off, try again"));
            return;
        }
    }
}

// Ends a move.
void PositionSync::Finish_cai(net::SessionEvent::Kind kind, const std::string& text)
{
    net::PositionStats& stats = mHub.Position_cai();
    stats.move = net::PositionStats::Move::None;
    stats.result = text;
    stats.reapplyWeather = true;                      // the weather and time again at the new place / after the reload
    stats.reapplyLoad = true;                         // and the PIC's fuel and payload (a reload resets them)
    LOG_INFO("position: {}", text);
    mHub.Note_cai(kind, text);
}

// X-Plane loaded the user's aircraft.
bool PositionSync::OnPlaneLoaded_cai()
{
    if (mHub.Position_cai().move != net::PositionStats::Move::Reloading)
        return false;
    if (mLoadedAt)
        return true;                                  // already seen (airport and aircraft messages)
    LOG_INFO("position: the new flight is loaded ({:.1f} s) - the session goes on", Since_cai(mStepAt));
    mLoadedAt = Clock::now();
    return true;
}

// From the flight loop.
void PositionSync::Tick_cai()
{
    net::NetSession& session = mHub.Session_cai();
    net::PositionStats& stats = mHub.Position_cai();
    if (!session.Active_cai()) {
        if (stats.move == net::PositionStats::Move::Moving && mRefs.overridePath)
            XPLMSetDatavi(mRefs.overridePath, &mOverrideBefore, 0, 1);   // never leave the flight model off
        // The position part only (the weather part belongs to WeatherSync).
        stats.known = stats.off = stats.ask = stats.moveRequested = stats.hold = false;
        stats.move = net::PositionStats::Move::None;
        stats.distanceM = 0.0;
        stats.result.clear();
        mAsked = false;
        return;
    }
    FindRefs_cai();
    const auto now = Clock::now();
    if (now >= mNextSend && stats.move != net::PositionStats::Move::Reloading) {
        mNextSend = now + std::chrono::milliseconds(static_cast<int>(net::kPositionInterval * 1000));
        const net::PositionInfo mine = ReadMine_cai();
        session.SendPosition_cai(mine);
        if (stats.move == net::PositionStats::Move::None)
            Compare_cai(mine);
    }
    if (stats.moveRequested) {
        stats.moveRequested = false;
        if (stats.move == net::PositionStats::Move::None)
            StartMove_cai();
    }
    StepMove_cai();
}

} // namespace sc::sync
