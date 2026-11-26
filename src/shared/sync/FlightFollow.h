// shared_cockpit - flying together, way 2: follow the PIC (0.9, the default)
//
// The PIC's aircraft leads. Its X-Plane reads where its aircraft is and how it moves - position,
// true heading / pitch / roll, the speeds in X-Plane's local frame, the rotation rates, on the
// ground or not - and sends it about 30 times a second (5 a second when standing still) on the
// unreliable channel, with its own clock.
// Every other X-Plane places its aircraft there every frame:
//   * the PIC's state is moved ahead by how old it is plus the network delay (the pings), along its
//     speeds and its heading / pitch / roll rates (worked out from the PIC's clock);
//   * the gap is closed smoothly: each frame by 1 - e^(-frame / smoothing) of it (session rule, 50 /
//     100 / 200 ms). Bigger than 30 m on the ground / 300 m in the air (a start, a jump): placed at once;
//   * the flight model keeps running (engine, systems, sounds and the gear react as usual): the
//     PIC's speeds and rates are written too, so it moves the same way between two frames. On the
//     ground the gear keeps my own height, pitch and roll (only the position and heading follow);
//   * strict (session rule followStrict): my flight model is off while following (X-Plane's
//     override_planepath), everything is placed - exact, but nothing of my own physics acts;
//   * right after a handover (I was the PIC): for kSoftS the gap is closed over at least
//     kSoftSmoothingS, so the few tenths of a metre between the two aircraft are not taken in 100 ms;
//   * no state for 1 s: "holding" - my aircraft flies on its own with the PIC's inputs until the
//     state comes back (then the gap is closed smoothly or at once, as above);
//   * farther than kFollowMaxM: not followed - "Move to PIC" (a new flight there) first.
// The PIC's yoke, pedals and levers still come over (FlightInputs, inside this class): the cockpit
// looks and sounds like the PIC's, and the follower's own hardware is switched off.

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "XPLMDataAccess.h"
#include "XPLMScenery.h"

#include "shared/net/SessionHub.h"
#include "shared/sync/FlightInputs.h"
#include "shared/sync/FlightSync.h"

namespace sc::sync {

class FlightFollow : public FlightMode {
public:
    explicit FlightFollow(net::SessionHub& hub);
    ~FlightFollow() override;

    FlightFollow(const FlightFollow&)            = delete;
    FlightFollow& operator=(const FlightFollow&) = delete;

    void Tick_cai(bool iAmPic) override;
    void OnPicChanged_cai(bool iAmPic) override;
    void Stop_cai() override;
    const char* Name_cai() const override { return "Follow"; }

private:
    using Clock = std::chrono::steady_clock;

    struct Refs {
        XPLMDataRef lat = nullptr, lon = nullptr, elev = nullptr, agl = nullptr;
        XPLMDataRef x = nullptr, y = nullptr, z = nullptr;
        XPLMDataRef psi = nullptr, theta = nullptr, phi = nullptr, q = nullptr;
        XPLMDataRef truePsi = nullptr, trueTheta = nullptr, truePhi = nullptr;
        XPLMDataRef vx = nullptr, vy = nullptr, vz = nullptr, p = nullptr, qRate = nullptr, r = nullptr;
        XPLMDataRef onGround = nullptr, groundSpeed = nullptr, paused = nullptr, frame = nullptr;
        XPLMDataRef overridePath = nullptr, eqlbm = nullptr;
    };
    // The PIC's state with the angle rates worked out from the one before.
    struct Lead {
        net::AircraftState state;
        std::uint32_t      sequence = 0, timeMs = 0;
        Clock::time_point  at;
        float              headingRate = 0.0f, pitchRate = 0.0f, rollRate = 0.0f;   // deg/s
    };

    // Finds the datarefs (once).
    void FindRefs_cai();
    // The PIC: reads my aircraft and sends it (30 a second, 5 when standing still).
    void Send_cai();
    // A follower: takes the PIC's newest state (a late, older one is dropped).
    void Take_cai();
    // A follower: places my aircraft at the PIC's (every frame).
    void Follow_cai();
    // My flight model off (strict following) or back to how it was.
    void Strict_cai(bool on);
    // The ground's height (local y) under a local point, if the scenery is there.
    std::optional<double> GroundY_cai(double x, double y, double z) const;
    // Writes the attitude (local heading, pitch, roll in degrees) as X-Plane's quaternion and angles.
    void WriteAttitude_cai(float psi, float theta, float phi) const;

    net::SessionHub&          mHub;
    FlightInputs              mInputs;              // the PIC's hands move my yoke and levers too
    Refs                      mRefs;
    bool                      mFound = false;
    XPLMProbeRef              mProbe = nullptr;
    // The PIC.
    Clock::time_point         mSentAt;
    Clock::time_point         mStart = Clock::now();   // my clock for timeMs
    // A follower.
    std::optional<Lead>       mLead;
    std::uint8_t              mLeadPic = net::kNoId;  // whose state mLead is
    bool                      mFollowing = false;   // placing my aircraft now (false: holding / not yet)
    bool                      mStrictOn = false;    // my flight model is off (strict)
    int                       mPathBefore = 0;      // override_planepath before strict switched it on
    bool                      mFarLogged = false;
    Clock::time_point         mJumpLogAt;           // the last "placed there at once" line
    bool                      mWasPic = false;      // the last OnPicChanged_cai
    Clock::time_point         mSoftUntil;           // after a handover: smoothing at least kSoftSmoothingS until then
};

} // namespace sc::sync
