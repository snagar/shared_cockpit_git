// shared_cockpit - seats: jump to one of the aircraft's vrconfig hotspots (0.9.6)
#include "shared/sync/SeatSync.h"

#include <cmath>

#include "XPLMPlanes.h"

#include "fmt/format.h"

#include "shared/analysis/VrSeats.h"
#include "shared/log/Log.h"
#include "shared/util/Utils.h"

namespace sc::sync {

namespace {

constexpr int   kView3dCockpit = 1026;          // sim/graphics/view/view_type: the 3D cockpit
constexpr float kSlideClose    = 0.01f;         // VR slide: close enough (m)
constexpr float kSlideSlowDown = 0.25f;         // VR slide: the normal speed from here (m)
constexpr float kStandingEyes  = 1.70f;         // a standing hotspot is the feet: the eyes above
constexpr float kFacingOff     = 15.0f;         // VR: a seat facing further off straight ahead gets a notice (deg)

} // namespace

// Finds the datarefs and makes the commands.
SeatSync::SeatSync(net::SessionHub& hub) : mHub(hub)
{
    mViewType = XPLMFindDataRef("sim/graphics/view/view_type");
    mVr       = XPLMFindDataRef("sim/graphics/VR/enabled");
    const char* head[6] = {"sim/graphics/view/pilots_head_x",   "sim/graphics/view/pilots_head_y",
                           "sim/graphics/view/pilots_head_z",   "sim/graphics/view/pilots_head_psi",
                           "sim/graphics/view/pilots_head_the", "sim/graphics/view/pilots_head_phi"};
    for (int i = 0; i < 6; ++i)
        mHead[i] = XPLMFindDataRef(head[i]);
    mCockpitView = XPLMFindCommand("sim/view/3d_cockpit_cmnd_look");
    mBack     = XPLMCreateCommand("shared_cockpit/seat/back_to_my_seat", "Shared Cockpit: back to my seat (3D cockpit view; VR: slides there)");
    mNext     = XPLMCreateCommand("shared_cockpit/seat/next", "Shared Cockpit: the next seat");
    mPrevious = XPLMCreateCommand("shared_cockpit/seat/previous", "Shared Cockpit: the previous seat");
    for (XPLMCommandRef command : {mBack, mNext, mPrevious})
        if (command)
            XPLMRegisterCommandHandler(command, Command_cai, 1, this);
    // VR slide: X-Plane's view-move commands (the arrow keys), and Reset VR view to stop one.
    const char* move[3][2] = {{"right", "left"}, {"up", "down"}, {"backward", "forward"}};
    for (int axis = 0; axis < 3; ++axis)
        for (int dir = 0; dir < 2; ++dir) {
            mMove[axis][dir][0] = XPLMFindCommand(fmt::format("sim/general/{}", move[axis][dir]).c_str());
            mMove[axis][dir][1] = XPLMFindCommand(fmt::format("sim/general/{}_fast", move[axis][dir]).c_str());
        }
    mOutside = XPLMFindDataRef("sim/graphics/VR/teleport_on_ground");
    mResetVr = XPLMFindCommand("sim/VR/general/reset_view");
    if (mResetVr)
        XPLMRegisterCommandHandler(mResetVr, ResetVr_cai, 1, this);
}

// Unregisters the commands.
SeatSync::~SeatSync()
{
    StopSlide_cai(nullptr);
    if (mResetVr)
        XPLMUnregisterCommandHandler(mResetVr, ResetVr_cai, 1, this);
    for (XPLMCommandRef command : {mBack, mNext, mPrevious})
        if (command)
            XPLMUnregisterCommandHandler(command, Command_cai, 1, this);
}

// The three seat commands.
int SeatSync::Command_cai(XPLMCommandRef command, XPLMCommandPhase phase, void* refcon)
{
    auto* self = static_cast<SeatSync*>(refcon);
    if (phase != xplm_CommandBegin || !self)
        return 1;
    if (command == self->mBack)
        self->BackToSeat_cai();
    else if (command == self->mNext)
        self->Step_cai(1);
    else if (command == self->mPrevious)
        self->Step_cai(-1);
    return 0;
}

// Reset VR view: stops a slide; X-Plane still resets the view (1 = pass it on).
int SeatSync::ResetVr_cai(XPLMCommandRef, XPLMCommandPhase phase, void* refcon)
{
    auto* self = static_cast<SeatSync*>(refcon);
    if (phase == xplm_CommandBegin && self && self->mSliding)
        self->StopSlide_cai("Reset VR view");
    return 1;
}

// Reads the seats of `acfFile` and gives them to the session.
void SeatSync::ReadSeats_cai(const std::filesystem::path& acfFile)
{
    std::vector<net::SeatInfo> seats;
    for (const analysis::VrSeat& seat : analysis::ReadVrSeats_cai(acfFile))
        seats.push_back({seat.name, seat.sitting, seat.x, seat.y, seat.z, seat.psi, seat.the, seat.phi});
    mHub.Session_cai().SetOwnSeats_cai(seats);
}

// From the flight loop.
void SeatSync::Tick_cai()
{
    const Clock::time_point now = Clock::now();
    if (now >= mNextCheck) {                       // the loaded aircraft, or its vrconfig file, changed
        mNextCheck = now + std::chrono::seconds(2);
        char fileName[256] = {};
        char path[1024]    = {};
        XPLMGetNthAircraftModel(0, fileName, path);
        if (path[0] != '\0') {
            const std::filesystem::path acf = Utils::PathFromUtf8_cai(path);
            const std::string stamp = Utils::FileStamp_cai(analysis::VrConfigPath_cai(acf));
            if (acf != mAcfFile || stamp != mStamp) {
                mAcfFile = acf;
                mStamp   = stamp;
                ReadSeats_cai(acf);
            }
        }
    }
    net::NetSession& session = mHub.Session_cai();
    if (session.TakeSeatsChanged_cai()) {
        mJumpFrames = 0;
        StopSlide_cai("the seats changed");
        LOG_INFO("seats to pick from: {} ({})", session.SessionSeats_cai().size(), Source_cai());
    }
    if (mRequested) {                              // from a window (Connect page)
        mRequested = false;
        Pick_cai(mRequestPick);
    }
    if (mRequestBack) {
        mRequestBack = false;
        BackToSeat_cai();
    }
    if (mJumpFrames > 0) {
        --mJumpFrames;
        Jump_cai();
    }
    if (mSliding)
        TickSlide_cai();
}

// The seats to pick from.
const std::vector<net::SeatInfo>& SeatSync::Seats_cai() const
{
    return mHub.Session_cai().SessionSeats_cai();
}

// My pick.
std::uint8_t SeatSync::MySeat_cai() const
{
    return mHub.Session_cai().MySeat_cai();
}

// True in VR.
bool SeatSync::InVr_cai() const
{
    return mVr && XPLMGetDatai(mVr) != 0;
}

// Where the seats come from.
std::string SeatSync::Source_cai() const
{
    const net::NetSession& session = mHub.Session_cai();
    if (session.Active_cai() && !session.IsHost_cai())
        return "the host's aircraft";
    return mAcfFile.empty() ? std::string("no aircraft") : Utils::Utf8_cai(analysis::VrConfigPath_cai(mAcfFile).filename());
}

// Picks a seat and jumps there.
void SeatSync::Pick_cai(std::uint8_t seat)
{
    net::NetSession& session = mHub.Session_cai();
    session.PickSeat_cai(seat);
    if (session.MySeat_cai() == net::kNoSeat)
        return;
    LOG_INFO("seat: {}", Seats_cai()[session.MySeat_cai()].name);
    mPicking = true;                                // a new seat: the window may follow (VR)
    BackToSeat_cai();
    mPicking = false;
}

// Back to my seat.
void SeatSync::BackToSeat_cai()
{
    const std::uint8_t seat = MySeat_cai();
    if (seat == net::kNoSeat || seat >= Seats_cai().size()) {
        mHub.Note_cai(net::SessionEvent::Kind::Notice, Seats_cai().empty() ? "This aircraft has no seats (vrconfig_helper can add them)"
                                                                            : "Pick a seat first (Flight deck)");
        return;
    }
    if (InVr_cai()) {
        StartSlide_cai();                           // VR: slide there (0.9.25)
        return;
    }
    if (mViewType && XPLMGetDatai(mViewType) != kView3dCockpit && mCockpitView)
        XPLMCommandOnce(mCockpitView);              // the head position is the 3D cockpit's
    mJumpFrames = 3;                                // a view change resets the head: again the next frames
    Jump_cai();
}

// The next / previous seat.
void SeatSync::Step_cai(int step)
{
    const int count = static_cast<int>(Seats_cai().size());
    if (count == 0) {
        BackToSeat_cai();                           // the notice
        return;
    }
    const std::uint8_t mine = MySeat_cai();
    const int from = mine == net::kNoSeat ? (step > 0 ? -1 : 0) : mine;
    Pick_cai(static_cast<std::uint8_t>(((from + step) % count + count) % count));
}

// Writes my seat's position and direction into the 3D cockpit view.
void SeatSync::Jump_cai()
{
    const std::uint8_t seat = MySeat_cai();
    if (seat >= Seats_cai().size() || InVr_cai() || (mViewType && XPLMGetDatai(mViewType) != kView3dCockpit))
        return;
    const net::SeatInfo& s = Seats_cai()[seat];
    const float values[6] = {s.x, s.y, s.z, s.psi, s.the, s.phi};
    for (int i = 0; i < 6; ++i)
        if (mHead[i])
            XPLMSetDataf(mHead[i], values[i]);
}

// The view-move command for `axis` in direction `sign` (+1 / -1).
XPLMCommandRef SeatSync::MoveCommand_cai(int axis, int sign, bool fast) const
{
    XPLMCommandRef command = mMove[axis][sign > 0 ? 0 : 1][fast ? 1 : 0];
    return command ? command : mMove[axis][sign > 0 ? 0 : 1][0];
}

// VR: starts sliding my view to my seat (all three axes at once).
void SeatSync::StartSlide_cai()
{
    StopSlide_cai(nullptr);
    const std::uint8_t seat = MySeat_cai();
    if (seat >= Seats_cai().size() || !mHead[0] || !mHead[1] || !mHead[2])
        return;
    if (mOutside && XPLMGetDatai(mOutside) != 0) {
        mHub.Note_cai(net::SessionEvent::Kind::Notice, "You are outside the aircraft: teleport back in first");
        return;
    }
    const net::SeatInfo& s = Seats_cai()[seat];
    mTarget[0] = s.x;
    mTarget[1] = s.sitting ? s.y : s.y + kStandingEyes;
    mTarget[2] = s.z;
    bool any = false;
    for (int axis = 0; axis < 3; ++axis) {
        Axis& a = mAxis[axis];
        const float d = mTarget[axis] - XPLMGetDataf(mHead[axis]);
        a = Axis{};
        if (std::fabs(d) <= kSlideClose)
            continue;
        a.sign = d > 0.0f ? 1 : -1;
        a.fast = std::fabs(d) > kSlideSlowDown;
        a.held = MoveCommand_cai(axis, a.sign, a.fast);
        a.done = a.held == nullptr;
        if (a.held) {
            XPLMCommandBegin(a.held);
            any = true;
        }
    }
    if (!any) {
        LOG_INFO("seat (VR): already at {}", s.name);
        return;
    }
    mSliding = true;
    mSlideBrings = mPicking;
    mSlideUntil = Clock::now() + std::chrono::seconds(10);
    LOG_INFO("seat (VR): sliding to {} ({:.2f}, {:.2f}, {:.2f})", s.name, mTarget[0], mTarget[1], mTarget[2]);
}

// VR: every flight loop of a slide: stops each axis once it reaches the seat, slows down near it.
void SeatSync::TickSlide_cai()
{
    if (!InVr_cai()) {
        StopSlide_cai("VR off");
        return;
    }
    if (mOutside && XPLMGetDatai(mOutside) != 0) {
        StopSlide_cai("teleported outside");
        return;
    }
    if (Clock::now() > mSlideUntil) {
        StopSlide_cai("did not reach the seat in 10 s");
        mHub.Note_cai(net::SessionEvent::Kind::Notice, "The view did not reach the seat (VR): try again, or use X-Plane's teleport");
        return;
    }
    bool all = true;
    for (int axis = 0; axis < 3; ++axis) {
        Axis& a = mAxis[axis];
        if (a.done)
            continue;
        const float left = (mTarget[axis] - XPLMGetDataf(mHead[axis])) * static_cast<float>(a.sign);
        if (left <= kSlideClose) {                  // there (or just past it)
            XPLMCommandEnd(a.held);
            a.held = nullptr;
            a.done = true;
            continue;
        }
        if (a.fast && left <= kSlideSlowDown) {     // near: the normal speed
            XPLMCommandEnd(a.held);
            a.fast = false;
            a.held = MoveCommand_cai(axis, a.sign, false);
            if (a.held)
                XPLMCommandBegin(a.held);
            else
                a.done = true;
        }
        all = all && a.done;
    }
    if (all) {
        mSliding = false;
        const std::uint8_t seat = MySeat_cai();
        if (seat >= Seats_cai().size())
            return;
        const net::SeatInfo& s = Seats_cai()[seat];
        LOG_INFO("seat (VR): at {} ({:.2f}, {:.2f}, {:.2f})", s.name, XPLMGetDataf(mHead[0]), XPLMGetDataf(mHead[1]),
                 XPLMGetDataf(mHead[2]));
        // VR cannot turn the view: say where this seat faces (0.9.26).
        const float facing = std::remainder(s.psi, 360.0f);            // -180..180, + = right
        if (std::fabs(facing) > kFacingOff)
            mHub.Note_cai(net::SessionEvent::Kind::Notice,
                          fmt::format("{} faces {:.0f}\u00b0 {}: turn your head that way (X-Plane's teleport sets the exact heading)",
                                      s.name, std::fabs(facing), facing > 0.0f ? "right" : "left"));
        if (mOnVrArrived && mSlideBrings)
            mOnVrArrived();
    }
}

// VR: releases every held move command (`why`: for the log, nullptr = quietly).
void SeatSync::StopSlide_cai(const char* why)
{
    for (Axis& a : mAxis) {
        if (a.held)
            XPLMCommandEnd(a.held);
        a = Axis{};
    }
    if (mSliding && why)
        LOG_INFO("seat (VR): slide stopped ({})", why);
    mSliding = false;
}

} // namespace sc::sync
