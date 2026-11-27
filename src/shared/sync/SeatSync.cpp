// shared_cockpit - seats: jump to one of the aircraft's vrconfig hotspots (0.9.6)
#include "shared/sync/SeatSync.h"

#include "XPLMPlanes.h"

#include "fmt/format.h"

#include "shared/analysis/VrSeats.h"
#include "shared/log/Log.h"
#include "shared/util/Utils.h"

namespace sc::sync {

namespace {

constexpr int kView3dCockpit = 1026;            // sim/graphics/view/view_type: the 3D cockpit

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
    mBack     = XPLMCreateCommand("shared_cockpit/seat/back_to_my_seat", "Shared Cockpit: back to my seat (2D / 3D cockpit view)");
    mNext     = XPLMCreateCommand("shared_cockpit/seat/next", "Shared Cockpit: the next seat");
    mPrevious = XPLMCreateCommand("shared_cockpit/seat/previous", "Shared Cockpit: the previous seat");
    for (XPLMCommandRef command : {mBack, mNext, mPrevious})
        if (command)
            XPLMRegisterCommandHandler(command, Command_cai, 1, this);
}

// Unregisters the commands.
SeatSync::~SeatSync()
{
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
        LOG_INFO("seats to pick from: {} ({})", session.SessionSeats_cai().size(), Source_cai());
    }
    if (mJumpFrames > 0) {
        --mJumpFrames;
        Jump_cai();
    }
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
    BackToSeat_cai();
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
        mHub.Note_cai(net::SessionEvent::Kind::Notice,
                      "In VR, use X-Plane's teleport to the seat (or leave VR, use Back to my seat, and return)");
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

} // namespace sc::sync
