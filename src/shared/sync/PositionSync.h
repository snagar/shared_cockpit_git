// shared_cockpit - where everybody's aircraft is, and the move to the PIC (0.6)
//
// While a session runs, every simulator sends its position once a second (the host sends
// them on). Each one compares its own with the PIC's:
//   * on the ground: farther than [position] ground_m (5 m), the heading more than
//     heading_deg (3°) off, or the engines differ          -> "Move to the PIC" is offered
//   * in the air:    farther than air_m (250 m)            -> offered
// Right after joining it asks once (Move / Not now); later the screens show a Move button
// only while the aircraft is off.
//
// The move:
//   * near (under 20 km) and the same engine state: a direct move - the flight model is
//     switched off (override_planepath), the position, attitude and speeds are written for
//     half a second, then it is switched on again. The cockpit is not touched.
//   * far, or the engines differ: a new flight at the PIC's position with the PIC's engine
//     state (XPLMInitFlight, X-Plane 12.4). That reloads the aircraft: the sync engines hold
//     (nothing sent), then the snapshot is asked again and written (the host writes back the
//     session's values it keeps).

#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "XPLMDataAccess.h"
#include "XPLMScenery.h"

#include "shared/net/SessionHub.h"
#include "shared/settings/Settings.h"

namespace sc::sync {

class PositionSync {
public:
    PositionSync(net::SessionHub& hub, const settings::Settings& settings);
    ~PositionSync();

    PositionSync(const PositionSync&)            = delete;
    PositionSync& operator=(const PositionSync&) = delete;

    // From the flight loop.
    void Tick_cai();
    // X-Plane loaded the user's aircraft. True when it is the reload of our own move (the
    // session goes on), false for anything else (the plugin leaves the session).
    bool OnPlaneLoaded_cai();

private:
    using Clock = std::chrono::steady_clock;

    struct Refs {
        XPLMDataRef lat = nullptr, lon = nullptr, elev = nullptr, agl = nullptr;
        XPLMDataRef x = nullptr, y = nullptr, z = nullptr;
        XPLMDataRef psi = nullptr, truePsi = nullptr, theta = nullptr, phi = nullptr, q = nullptr;
        XPLMDataRef vx = nullptr, vy = nullptr, vz = nullptr, p = nullptr, qRate = nullptr, r = nullptr;
        XPLMDataRef groundSpeed = nullptr, onGround = nullptr, running = nullptr, engineCount = nullptr;
        XPLMDataRef overridePath = nullptr;
        XPLMDataRef eqlbm = nullptr, gNormal = nullptr;     // height on the gear, the g-load (after a move)
    };
    // The move being done.
    struct Target {
        net::PositionInfo pos;
        double x = 0, y = 0, z = 0;         // local OpenGL coordinates
        float  vx = 0, vy = 0, vz = 0;
        std::optional<double> terrainY;     // the probed ground (on the ground), re-probed while holding
    };

    // Finds the datarefs (once).
    void FindRefs_cai();
    // My position now.
    net::PositionInfo ReadMine_cai() const;
    // Compares mine with the PIC's (PositionStats).
    void Compare_cai(const net::PositionInfo& mine);
    // Starts a move to the PIC (the screens asked).
    void StartMove_cai();
    // The direct move: target in local coordinates (the ground under it probed).
    bool MakeTarget_cai(const net::PositionInfo& pic, double ageSeconds, Target& target);
    // My aircraft's centre above the ground on its gear (never the PIC's unless nothing else works).
    double OwnHeightOnGear_cai(double picAgl);
    // Writes the target with the flight model off (each frame of the move).
    void WriteTarget_cai(const Target& target);
    // A new flight at the PIC's position (XPLMInitFlight). False with `why`.
    bool InitFlight_cai(const net::PositionInfo& pic, std::string& why);
    // The steps of a move, every frame.
    void StepMove_cai();
    // Ends a move with a line in the feed.
    void Finish_cai(net::SessionEvent::Kind kind, const std::string& text);
    // The PIC's last position (empty when unknown, old or I am the PIC).
    std::optional<net::NetSession::KnownPosition> PicPosition_cai() const;

    net::SessionHub&          mHub;
    const settings::Settings& mSettings;
    Refs                      mRefs;
    bool                      mFound = false;
    XPLMProbeRef              mProbe = nullptr;
    Clock::time_point         mNextSend;
    bool                      mAsked = false;          // the question after joining was shown
    // The move.
    Target                    mTarget;
    int                       mFrames = 0;
    float                     mMinAgl = 0.0f, mMaxG = 0.0f;   // watched after the flight model is back
    int                       mOverrideBefore = 0;
    bool                      mFar = false;            // a new flight (reload), not a direct move
    Clock::time_point         mStepAt;                 // when the current step began
    std::optional<Clock::time_point> mLoadedAt;        // the reload finished (XPLM_MSG_PLANE_LOADED)
    bool                      mSnapshotAsked = false;
};

} // namespace sc::sync
