// shared_cockpit - flying together (0.8)
//
// FlightSync is what both ways of flying together share:
//   * which way the session flies (the session rule SessionRules::flight) - it runs one FlightMode:
//       Follow (FlightFollow.h): the PIC's aircraft leads, the others are placed where it is every
//                                frame (the PIC's inputs still move their yokes and levers);
//       Inputs (FlightInputs.h): every X-Plane flies its own flight model, the PIC's yoke, rudder,
//                                brakes and levers go to everybody (the aircraft drift apart).
//   * whether I am the PIC (the mode is told when that changes);
//   * pause: the PIC's pause state is everybody's. The PIC's X-Plane sends it when it changes; the
//     others pause / resume to match. Anybody else's pause command (key, menu, button: the commands
//     sim/operation/pause_toggle, pause_on, pause_off) is stopped right away - it never pauses;
//     a pause from anywhere else is undone ("Only the PIC pauses the flight").

#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"

#include "shared/net/SessionHub.h"

namespace sc::sync {

// One way of flying together (Follow or Inputs).
class FlightMode {
public:
    virtual ~FlightMode() = default;
    // Every frame while the session runs.
    virtual void Tick_cai(bool iAmPic) = 0;
    // I became the PIC (true) or stopped being it (false).
    virtual void OnPicChanged_cai(bool iAmPic) = 0;
    // The session ended or the mode changes: give everything back to X-Plane.
    virtual void Stop_cai() = 0;
    // "Inputs" / "Follow" for the log and the screens.
    virtual const char* Name_cai() const = 0;
};

class FlightSync {
public:
    explicit FlightSync(net::SessionHub& hub);
    ~FlightSync();

    FlightSync(const FlightSync&)            = delete;
    FlightSync& operator=(const FlightSync&) = delete;

    // From the flight loop.
    void Tick_cai();
    // Stops (the plugin is disabled).
    void Stop_cai(const char* why);

private:
    using Clock = std::chrono::steady_clock;

    // Makes the mode the session's rule asks for.
    void ChooseMode_cai();
    // Pause: the PIC sends, the others follow.
    void TickPause_cai(bool iAmPic);
    // Pauses or resumes this X-Plane (its own commands).
    void SetPaused_cai(bool paused);
    // True when this X-Plane is paused now.
    bool Paused_cai() const;
    // The pause commands: stopped while I am in a session and not the PIC (unless I match the PIC).
    static int PauseCommand_cai(XPLMCommandRef command, XPLMCommandPhase phase, void* refcon);
    // True when this pause command should be stopped now: it would take me away from the PIC's state.
    bool BlockPause_cai(XPLMCommandRef command) const;

    net::SessionHub&            mHub;
    std::unique_ptr<FlightMode> mMode;
    net::FlightMode             mModeKind = net::FlightMode::Inputs;
    bool                        mRunning = false;
    bool                        mWasPic = false;
    // Pause.
    XPLMDataRef                 mPausedRef = nullptr;
    XPLMCommandRef              mPauseOn = nullptr, mPauseOff = nullptr, mPauseToggle = nullptr;
    std::optional<bool>         mPicPaused;           // the PIC's state (a follower)
    std::optional<bool>         mSentPaused;          // the state I sent (the PIC)
    Clock::time_point           mAppliedAt;           // when I last paused / resumed to match the PIC
    bool                        mApplying = false;    // I run a pause command myself (matching the PIC)
    bool                        mHandlers = false;    // the pause command handlers are registered
    Clock::time_point           mBlockedNoteAt;       // the last "Only the PIC pauses" line
};

} // namespace sc::sync
