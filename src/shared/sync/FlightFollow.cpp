// shared_cockpit - flying together, way 2: follow the PIC (0.9)

#include "shared/sync/FlightFollow.h"

#include <algorithm>
#include <cmath>

#include "XPLMGraphics.h"

#include "shared/log/Log.h"
#include "shared/util/Utils.h"

namespace sc::sync {

namespace {

constexpr double kRad         = 3.14159265358979323846 / 180.0;
constexpr double kHoldS       = 1.0;     // no state for this long: holding (flying on my own)
constexpr double kAheadMaxS   = 0.5;     // the PIC's state is moved ahead at most this far
constexpr double kJumpGroundM = 30.0;    // a bigger gap is closed at once (on the ground)
constexpr double kJumpAirM    = 300.0;   //   ... in the air
constexpr double kStillRate   = 5.0;     // states a second while the PIC stands still
constexpr float  kMaxRate     = 90.0f;   // deg/s: a faster worked-out angle rate is a glitch
constexpr double kSoftS          = 1.5;  // after a handover (I was the PIC): softer this long
constexpr double kSoftSmoothingS = 0.5;  //   ... the gap closed over at least this

// Seconds between two times.
double Seconds_cai(std::chrono::steady_clock::duration d)
{
    return std::chrono::duration<double>(d).count();
}

// a - b in degrees, -180..180.
float AngleDiff_cai(float a, float b)
{
    float d = std::fmod(a - b, 360.0f);
    if (d > 180.0f)
        d -= 360.0f;
    if (d < -180.0f)
        d += 360.0f;
    return d;
}

} // namespace

FlightFollow::FlightFollow(net::SessionHub& hub) : mHub(hub), mInputs(hub)
{
}

FlightFollow::~FlightFollow()
{
    Strict_cai(false);
    if (mProbe)
        XPLMDestroyProbe(mProbe);
}

// Finds the datarefs (once).
void FlightFollow::FindRefs_cai()
{
    if (mFound)
        return;
    mFound = true;
    auto lmbda_find = [](const char* name) {
        XPLMDataRef ref = XPLMFindDataRef(name);
        if (!ref)
            LOG_WARN("follow: dataref {} not found", name);
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
    r.theta = lmbda_find("sim/flightmodel/position/theta");
    r.phi = lmbda_find("sim/flightmodel/position/phi");
    r.q = lmbda_find("sim/flightmodel/position/q");
    r.truePsi = lmbda_find("sim/flightmodel/position/true_psi");
    r.trueTheta = lmbda_find("sim/flightmodel/position/true_theta");
    r.truePhi = lmbda_find("sim/flightmodel/position/true_phi");
    r.vx = lmbda_find("sim/flightmodel/position/local_vx");
    r.vy = lmbda_find("sim/flightmodel/position/local_vy");
    r.vz = lmbda_find("sim/flightmodel/position/local_vz");
    r.p = lmbda_find("sim/flightmodel/position/P");
    r.qRate = lmbda_find("sim/flightmodel/position/Q");
    r.r = lmbda_find("sim/flightmodel/position/R");
    r.onGround = lmbda_find("sim/flightmodel/failures/onground_any");
    r.groundSpeed = lmbda_find("sim/flightmodel/position/groundspeed");
    r.paused = lmbda_find("sim/time/paused");
    r.frame = lmbda_find("sim/operation/misc/frame_rate_period");
    r.overridePath = lmbda_find("sim/operation/override/override_planepath");
    r.eqlbm = lmbda_find("sim/aircraft/gear/acf_h_eqlbm");
    mProbe = XPLMCreateProbe(xplm_ProbeY);
}

// Every frame while the session runs.
void FlightFollow::Tick_cai(bool iAmPic)
{
    FindRefs_cai();
    mInputs.Tick_cai(iAmPic);
    net::FlightStats& stats = mHub.Flight_cai();
    stats.mode = Name_cai();
    if (iAmPic) {
        Send_cai();
        return;
    }
    Take_cai();
    Follow_cai();
}

// I became the PIC or stopped being it: start over.
void FlightFollow::OnPicChanged_cai(bool iAmPic)
{
    mInputs.OnPicChanged_cai(iAmPic);
    if (!iAmPic && mWasPic)
        mSoftUntil = Clock::now() + std::chrono::milliseconds(static_cast<int>(kSoftS * 1000.0));
    mWasPic = iAmPic;
    Strict_cai(false);
    mLead.reset();
    mFollowing = false;
    mFarLogged = false;
    net::FlightStats& stats = mHub.Flight_cai();
    stats.following = stats.holding = stats.farAway = stats.strict = false;
    LOG_INFO("follow: {}", iAmPic ? "I am the PIC - the others follow my aircraft" : "following the PIC's aircraft");
}

// The session ended or the mode changes.
void FlightFollow::Stop_cai()
{
    mInputs.Stop_cai();
    Strict_cai(false);
    mLead.reset();
    mFollowing = false;
    net::FlightStats& stats = mHub.Flight_cai();
    stats.following = stats.holding = stats.farAway = stats.strict = false;
}

// The PIC: reads my aircraft and sends it.
void FlightFollow::Send_cai()
{
    const Refs& r = mRefs;
    const auto now = Clock::now();
    const bool still = (r.onGround && XPLMGetDatai(r.onGround) != 0) && (r.groundSpeed && XPLMGetDataf(r.groundSpeed) < 0.1f);
    if (Seconds_cai(now - mSentAt) < 1.0 / (still ? kStillRate : net::kFlightStateRate))
        return;
    mSentAt = now;
    net::AircraftState s;
    s.latitude = r.lat ? XPLMGetDatad(r.lat) : 0.0;
    s.longitude = r.lon ? XPLMGetDatad(r.lon) : 0.0;
    s.elevation = r.elev ? XPLMGetDatad(r.elev) : 0.0;
    s.heading = r.truePsi ? XPLMGetDataf(r.truePsi) : (r.psi ? XPLMGetDataf(r.psi) : 0.0f);
    s.pitch = r.trueTheta ? XPLMGetDataf(r.trueTheta) : (r.theta ? XPLMGetDataf(r.theta) : 0.0f);
    s.roll = r.truePhi ? XPLMGetDataf(r.truePhi) : (r.phi ? XPLMGetDataf(r.phi) : 0.0f);
    s.vx = r.vx ? XPLMGetDataf(r.vx) : 0.0f;
    s.vy = r.vy ? XPLMGetDataf(r.vy) : 0.0f;
    s.vz = r.vz ? XPLMGetDataf(r.vz) : 0.0f;
    s.p = r.p ? XPLMGetDataf(r.p) : 0.0f;
    s.q = r.qRate ? XPLMGetDataf(r.qRate) : 0.0f;
    s.r = r.r ? XPLMGetDataf(r.r) : 0.0f;
    s.agl = r.agl ? XPLMGetDataf(r.agl) : 0.0f;
    s.onGround = r.onGround ? XPLMGetDatai(r.onGround) != 0 : true;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - mStart).count();
    mHub.Session_cai().SendFlightState_cai(s, static_cast<std::uint32_t>(ms));
}

// A follower: takes the PIC's newest state.
void FlightFollow::Take_cai()
{
    net::NetSession& session = mHub.Session_cai();
    if (session.PicId_cai() != mLeadPic) {
        // Another PIC (between two others): its numbers and clock start anew.
        mLeadPic = session.PicId_cai();
        mLead.reset();
    }
    auto in = session.TakeFlightState_cai();
    if (!in)
        return;
    if (mLead && static_cast<std::int32_t>(in->sequence - mLead->sequence) <= 0)
        return;                                       // an older one, late (unreliable channel)
    Lead lead;
    lead.state = in->state;
    lead.sequence = in->sequence;
    lead.timeMs = in->timeMs;
    lead.at = in->at;
    if (mLead) {
        // The angle rates from the PIC's own clock between the two states.
        const double dt = static_cast<std::int32_t>(in->timeMs - mLead->timeMs) / 1000.0;
        if (dt > 0.01 && dt < 1.0) {
            auto lmbda_rate = [dt](float now, float before) {
                return std::clamp(static_cast<float>(AngleDiff_cai(now, before) / dt), -kMaxRate, kMaxRate);
            };
            lead.headingRate = lmbda_rate(in->state.heading, mLead->state.heading);
            lead.pitchRate = lmbda_rate(in->state.pitch, mLead->state.pitch);
            lead.rollRate = lmbda_rate(in->state.roll, mLead->state.roll);
        } else {
            lead.headingRate = mLead->headingRate;
            lead.pitchRate = mLead->pitchRate;
            lead.rollRate = mLead->rollRate;
        }
    }
    mLead = lead;
    mHub.Flight_cai().stateAt = in->at;
}

// The ground's height under a local point.
std::optional<double> FlightFollow::GroundY_cai(double x, double y, double z) const
{
    if (!mProbe)
        return std::nullopt;
    XPLMProbeInfo_t info;
    info.structSize = sizeof(info);
    if (XPLMProbeTerrainXYZ(mProbe, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), &info) != xplm_ProbeHitTerrain)
        return std::nullopt;
    return info.locationY;
}

// Writes the attitude (local frame, degrees) as X-Plane's quaternion and its angles.
void FlightFollow::WriteAttitude_cai(float psi, float theta, float phi) const
{
    const Refs& r = mRefs;
    if (r.psi) XPLMSetDataf(r.psi, psi);
    if (r.theta) XPLMSetDataf(r.theta, theta);
    if (r.phi) XPLMSetDataf(r.phi, phi);
    if (!r.q)
        return;
    const double h = psi * kRad / 2.0, t = theta * kRad / 2.0, f = phi * kRad / 2.0;
    float q[4];
    q[0] = static_cast<float>(std::cos(h) * std::cos(t) * std::cos(f) + std::sin(h) * std::sin(t) * std::sin(f));
    q[1] = static_cast<float>(std::cos(h) * std::cos(t) * std::sin(f) - std::sin(h) * std::sin(t) * std::cos(f));
    q[2] = static_cast<float>(std::cos(h) * std::sin(t) * std::cos(f) + std::sin(h) * std::cos(t) * std::sin(f));
    q[3] = static_cast<float>(-std::cos(h) * std::sin(t) * std::sin(f) + std::sin(h) * std::cos(t) * std::cos(f));
    XPLMSetDatavf(r.q, q, 0, 4);
}

// My flight model off (strict following) or back to how it was.
void FlightFollow::Strict_cai(bool on)
{
    if (on == mStrictOn || !mRefs.overridePath)
        return;
    if (on) {
        XPLMGetDatavi(mRefs.overridePath, &mPathBefore, 0, 1);
        int one = 1;
        XPLMSetDatavi(mRefs.overridePath, &one, 0, 1);
        LOG_INFO("follow: strict - my flight model is off while following");
    } else {
        XPLMSetDatavi(mRefs.overridePath, &mPathBefore, 0, 1);
        LOG_INFO("follow: my flight model is back on");
    }
    mStrictOn = on;
    mHub.Flight_cai().strict = on;
}

// A follower: places my aircraft at the PIC's.
void FlightFollow::Follow_cai()
{
    const Refs& r = mRefs;
    net::FlightStats& stats = mHub.Flight_cai();
    net::NetSession& session = mHub.Session_cai();
    const net::SessionRules& rules = session.Rules_cai();
    const auto now = Clock::now();
    if (!r.x || !r.y || !r.z || !r.lat) {
        stats.following = false;
        return;
    }
    // A move to the PIC (PositionSync) or a reload runs: it places the aircraft itself.
    if (mHub.Position_cai().move != net::PositionStats::Move::None || mHub.Position_cai().hold) {
        Strict_cai(false);
        mFollowing = stats.following = false;
        return;
    }
    if (r.paused && XPLMGetDatai(r.paused) != 0)
        return;                                       // paused like the PIC: nothing moves
    const double age = mLead ? Seconds_cai(now - mLead->at) : 1e9;
    if (age > kHoldS) {
        if (mFollowing)
            LOG_INFO("follow: no state from the PIC for {:.1f} s - flying on my own until it comes back", age);
        Strict_cai(false);
        mFollowing = stats.following = false;
        stats.holding = mLead.has_value();
        return;
    }
    stats.holding = false;
    const net::AircraftState& s = mLead->state;
    const double delayS = session.PicDelayMs_cai() / 1000.0;
    const double ahead = std::clamp(age + delayS, 0.0, kAheadMaxS);
    stats.delayMs = static_cast<float>((age + delayS) * 1000.0);
    // Where the PIC's aircraft is now (its last state moved ahead).
    double tx = 0, ty = 0, tz = 0;
    XPLMWorldToLocal(s.latitude, s.longitude, s.elevation, &tx, &ty, &tz);
    tx += s.vx * ahead;
    ty += s.vy * ahead;
    tz += s.vz * ahead;
    const float trueHeading = s.heading + static_cast<float>(mLead->headingRate * ahead);
    const float targetPitch = s.pitch + static_cast<float>(mLead->pitchRate * ahead);
    const float targetRoll = s.roll + static_cast<float>(mLead->rollRate * ahead);
    const double x = XPLMGetDatad(r.x), y = XPLMGetDatad(r.y), z = XPLMGetDatad(r.z);
    const double gap = std::hypot(tx - x, tz - z);
    // My local frame's north differs a little from true north: the same offset for the PIC's heading.
    const float myPsi = r.psi ? XPLMGetDataf(r.psi) : 0.0f;
    const float offset = r.truePsi ? AngleDiff_cai(myPsi, XPLMGetDataf(r.truePsi)) : 0.0f;
    const float targetPsi = trueHeading + offset;
    stats.gapM = static_cast<float>(gap);
    stats.gapDeg = std::fabs(AngleDiff_cai(targetPsi, myPsi));
    if (gap > net::kFollowMaxM) {
        if (!mFarLogged)
            LOG_INFO("follow: {} from the PIC - too far to follow, 'Move to PIC' first", Utils::DistanceText_cai(gap));
        mFarLogged = true;
        Strict_cai(false);
        mFollowing = stats.following = false;
        stats.farAway = true;
        return;
    }
    stats.farAway = false;
    mFarLogged = false;
    const bool strict = rules.followStrict;
    Strict_cai(strict);
    // How much of the gap this frame closes.
    const double frame = std::clamp(r.frame ? static_cast<double>(XPLMGetDataf(r.frame)) : 0.02, 0.001, 0.2);
    double tau = std::max(0.02, rules.smoothingMs / 1000.0);
    if (now < mSoftUntil)
        tau = std::max(tau, kSoftSmoothingS);         // just handed over: take the gap in gently
    const double jump = s.onGround ? kJumpGroundM : kJumpAirM;
    double alpha = 1.0 - std::exp(-frame / tau);
    if (gap > jump || std::fabs(ty - y) > jump) {
        if (Seconds_cai(now - mJumpLogAt) > 2.0) {
            mJumpLogAt = now;
            LOG_INFO("follow: {} from the PIC's aircraft - placed there at once", Utils::DistanceText_cai(std::max(gap, std::fabs(ty - y))));
        }
        alpha = 1.0;
    }
    if (!mFollowing)
        LOG_INFO("follow: following the PIC's aircraft ({} away, {:.0f}° off, {:.0f} ms behind{})",
                 Utils::DistanceText_cai(gap), stats.gapDeg, stats.delayMs, strict ? ", strict" : "");
    mFollowing = stats.following = true;
    stats.strict = strict;
    const bool myGround = r.onGround && XPLMGetDatai(r.onGround) != 0;
    const float eqlbm = r.eqlbm ? XPLMGetDataf(r.eqlbm) : 0.0f;
    // Position.
    XPLMSetDatad(r.x, x + (tx - x) * alpha);
    XPLMSetDatad(r.z, z + (tz - z) * alpha);
    if (s.onGround) {
        // On the ground the gear holds my height; placed onto it only when I am off it (a jump, strict).
        const auto ground = GroundY_cai(tx, y, tz);
        if (ground && eqlbm > 0.1f && (strict || !myGround || alpha >= 1.0))
            XPLMSetDatad(r.y, *ground + eqlbm);
    } else {
        XPLMSetDatad(r.y, y + (ty - y) * alpha);
    }
    // Attitude: on the ground my gear sets pitch and roll (unless strict).
    const float myTheta = r.theta ? XPLMGetDataf(r.theta) : 0.0f, myPhi = r.phi ? XPLMGetDataf(r.phi) : 0.0f;
    const float a = static_cast<float>(alpha);
    const float psi = myPsi + AngleDiff_cai(targetPsi, myPsi) * a;
    const bool ownAttitude = s.onGround && !strict;
    const float theta = ownAttitude ? myTheta : myTheta + AngleDiff_cai(targetPitch, myTheta) * a;
    const float phi = ownAttitude ? myPhi : myPhi + AngleDiff_cai(targetRoll, myPhi) * a;
    WriteAttitude_cai(psi, theta, phi);
    // Speeds and rates: the PIC's, so my aircraft moves the same way until the next frame.
    if (r.vx) XPLMSetDataf(r.vx, s.vx);
    if (r.vz) XPLMSetDataf(r.vz, s.vz);
    if (r.vy && (!s.onGround || strict)) XPLMSetDataf(r.vy, s.vy);
    if (r.r) XPLMSetDataf(r.r, s.r);
    if (!ownAttitude) {
        if (r.p) XPLMSetDataf(r.p, s.p);
        if (r.qRate) XPLMSetDataf(r.qRate, s.q);
    }
}

} // namespace sc::sync
