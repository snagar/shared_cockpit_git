// shared_cockpit - seats: jump to one of the aircraft's vrconfig hotspots (0.9.6)
//
// The seats come only from the loaded aircraft's vrconfig file (analysis/VrSeats); the host's are
// sent to everybody (kept in memory for the session, no file written), so the whole crew picks
// from the same list. Picking a seat moves my 3D cockpit view there (pilot's head position and
// direction); "Back to my seat" (also a command) puts it back after looking around. Everybody's
// pick shows on the Flight deck and the Connect page. The seat is only my view: it changes
// nothing for the others.
//
// VR (0.9.25): the headset owns the head, so writing it does nothing there. Instead the view
// slides to the seat with X-Plane's own view-move commands (sim/general/right, up, backward, ...,
// the _fast ones while far), held until pilots_head_x/y/z reaches the seat: the same as holding
// the arrow keys. Only the position: the headset decides where you look. Reset VR view, VR off,
// a teleport outside or 10 s stop a slide.
//
// Commands: shared_cockpit/seat/back_to_my_seat, shared_cockpit/seat/next, shared_cockpit/seat/previous.

#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"

#include "shared/net/SessionHub.h"

namespace sc::sync {

class SeatSync {
public:
    explicit SeatSync(net::SessionHub& hub);
    ~SeatSync();

    SeatSync(const SeatSync&)            = delete;
    SeatSync& operator=(const SeatSync&) = delete;

    // From the flight loop: follows the loaded aircraft's seats, does a pending jump.
    void Tick_cai();

    // The seats to pick from (a joiner: the host's).
    const std::vector<net::SeatInfo>& Seats_cai() const;
    // My pick (net::kNoSeat: none).
    std::uint8_t MySeat_cai() const;
    // Picks a seat and jumps there (kNoSeat: none, no jump).
    void Pick_cai(std::uint8_t seat);
    // Back to my seat's position and direction (no seat: a notice).
    void BackToSeat_cai();
    // The next / previous seat (wraps).
    void Step_cai(int step);
    // True in VR (no jump there).
    bool InVr_cai() const;
    // Where the seats come from, for the screens ("Cessna_172SP_vrconfig.txt", "the host's").
    std::string Source_cai() const;
    // From a window's draw: a pick / back done in the next flight loop (0.9.25).
    void RequestPick_cai(std::uint8_t seat) { mRequestPick = seat; mRequested = true; }
    void RequestBack_cai() { mRequestBack = true; }
    // True while the VR view slides to a seat.
    bool Sliding_cai() const { return mSliding; }
    // Called when a VR slide after a seat pick reaches the seat (0.9.26: the main window comes to me;
    // 0.9.27: not after Back to my seat).
    void SetOnVrArrived_cai(std::function<void()> arrived) { mOnVrArrived = std::move(arrived); }

private:
    using Clock = std::chrono::steady_clock;

    // One axis of a VR slide: the move command held now, its direction, fast or not.
    struct Axis {
        XPLMCommandRef held = nullptr;
        int            sign = 0;
        bool           fast = false;
        bool           done = true;
    };

    static int Command_cai(XPLMCommandRef command, XPLMCommandPhase phase, void* refcon);
    static int ResetVr_cai(XPLMCommandRef command, XPLMCommandPhase phase, void* refcon);
    void ReadSeats_cai(const std::filesystem::path& acfFile);
    void Jump_cai();
    void StartSlide_cai();
    void TickSlide_cai();
    void StopSlide_cai(const char* why);
    XPLMCommandRef MoveCommand_cai(int axis, int sign, bool fast) const;

    net::SessionHub&      mHub;
    XPLMCommandRef        mBack = nullptr, mNext = nullptr, mPrevious = nullptr;
    XPLMDataRef           mViewType = nullptr, mVr = nullptr;
    XPLMDataRef           mHead[6] = {};           // pilots_head_x/y/z, _psi/_the/_phi
    XPLMCommandRef        mCockpitView = nullptr;  // sim/view/3d_cockpit_cmnd_look
    std::filesystem::path mAcfFile;
    std::string           mStamp;                  // the vrconfig file's stamp when read
    Clock::time_point     mNextCheck{};
    int                   mJumpFrames = 0;         // frames left to (re)apply a jump
    // VR slide (0.9.25): [axis][0 = +, 1 = -][0 = normal, 1 = fast]; x right, y up, z aft.
    XPLMCommandRef        mMove[3][2][2] = {};
    XPLMCommandRef        mResetVr = nullptr;      // sim/VR/general/reset_view: stops a slide
    XPLMDataRef           mOutside = nullptr;      // sim/graphics/VR/teleport_on_ground
    Axis                  mAxis[3];
    float                 mTarget[3] = {};
    bool                  mSliding = false;
    Clock::time_point     mSlideUntil{};
    bool                  mRequested = false, mRequestBack = false;
    std::uint8_t          mRequestPick = 0;
    std::function<void()> mOnVrArrived;
    bool                  mPicking = false;        // a pick (not Back to my seat) starts the slide (0.9.27)
    bool                  mSlideBrings = false;    // this slide brings the window when it arrives
};

} // namespace sc::sync
