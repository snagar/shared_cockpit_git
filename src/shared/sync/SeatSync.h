// shared_cockpit - seats: jump to one of the aircraft's vrconfig hotspots (0.9.6)
//
// The seats come only from the loaded aircraft's vrconfig file (analysis/VrSeats); the host's are
// sent to everybody (kept in memory for the session, no file written), so the whole crew picks
// from the same list. Picking a seat moves my 3D cockpit view there (pilot's head position and
// direction); "Back to my seat" (also a command) puts it back after looking around. Everybody's
// pick shows on the Flight deck and the Connect page. The seat is only my view: it changes
// nothing for the others.
//
// VR: the headset owns the head, so the plugin does not move it; a VR user can use X-Plane's own
// teleport, or leave VR, use "Back to my seat" and return.
//
// Commands: shared_cockpit/seat/back_to_my_seat, shared_cockpit/seat/next, shared_cockpit/seat/previous.

#pragma once

#include <chrono>
#include <filesystem>
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

private:
    using Clock = std::chrono::steady_clock;

    static int Command_cai(XPLMCommandRef command, XPLMCommandPhase phase, void* refcon);
    void ReadSeats_cai(const std::filesystem::path& acfFile);
    void Jump_cai();

    net::SessionHub&      mHub;
    XPLMCommandRef        mBack = nullptr, mNext = nullptr, mPrevious = nullptr;
    XPLMDataRef           mViewType = nullptr, mVr = nullptr;
    XPLMDataRef           mHead[6] = {};           // pilots_head_x/y/z, _psi/_the/_phi
    XPLMCommandRef        mCockpitView = nullptr;  // sim/view/3d_cockpit_cmnd_look
    std::filesystem::path mAcfFile;
    std::string           mStamp;                  // the vrconfig file's stamp when read
    Clock::time_point     mNextCheck{};
    int                   mJumpFrames = 0;         // frames left to (re)apply a jump
};

} // namespace sc::sync
