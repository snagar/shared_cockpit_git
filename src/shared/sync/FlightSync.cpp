// shared_cockpit - flying together (0.8)

#include "shared/sync/FlightSync.h"

#include "shared/log/Log.h"
#include "shared/sync/FlightFollow.h"
#include "shared/sync/FlightInputs.h"

namespace sc::sync {

namespace {
constexpr double kPauseGraceS = 0.5;   // after matching the PIC, my own pause state settles this long
}

FlightSync::FlightSync(net::SessionHub& hub) : mHub(hub)
{
    mPausedRef = XPLMFindDataRef("sim/time/paused");
    mPauseOn = XPLMFindCommand("sim/operation/pause_on");
    mPauseOff = XPLMFindCommand("sim/operation/pause_off");
    mPauseToggle = XPLMFindCommand("sim/operation/pause_toggle");
    for (XPLMCommandRef command : {mPauseOn, mPauseOff, mPauseToggle})
        if (command)
            XPLMRegisterCommandHandler(command, PauseCommand_cai, 1, this);   // before X-Plane: can stop it
    mHandlers = true;
}

FlightSync::~FlightSync()
{
    Stop_cai("the plugin stopped");
    if (mHandlers)
        for (XPLMCommandRef command : {mPauseOn, mPauseOff, mPauseToggle})
            if (command)
                XPLMUnregisterCommandHandler(command, PauseCommand_cai, 1, this);
}

// True when this pause command should be stopped: in a session, not the PIC, and it would take me
// away from the PIC's pause state (towards it is fine: that is how I match the PIC, whenever X-Plane runs it).
bool FlightSync::BlockPause_cai(XPLMCommandRef command) const
{
    const net::NetSession& session = mHub.Session_cai();
    if (!mRunning || mApplying || !session.Active_cai() || session.PicId_cai() == session.MyId_cai())
        return false;
    const bool picPaused = mPicPaused.value_or(Paused_cai());   // not known yet: keep as I am
    const bool paused = Paused_cai();
    const bool after = command == mPauseOn ? true : command == mPauseOff ? false : !paused;
    return after != picPaused;
}

// The pause commands (registered before X-Plane handles them).
int FlightSync::PauseCommand_cai(XPLMCommandRef command, XPLMCommandPhase phase, void* refcon)
{
    auto* self = static_cast<FlightSync*>(refcon);
    if (!self->BlockPause_cai(command))
        return 1;                                     // let X-Plane pause / resume
    if (phase == xplm_CommandBegin) {
        const auto now = Clock::now();
        LOG_INFO("pause: stopped - only the PIC pauses the flight");
        if (now - self->mBlockedNoteAt > std::chrono::seconds(2)) {
            self->mBlockedNoteAt = now;
            self->mHub.Note_cai(net::SessionEvent::Kind::Declined, "Only the PIC pauses the flight");
        }
    }
    return 0;                                         // eaten: X-Plane never sees it
}

// True when this X-Plane is paused now.
bool FlightSync::Paused_cai() const
{
    return mPausedRef && XPLMGetDatai(mPausedRef) != 0;
}

// Pauses or resumes this X-Plane.
void FlightSync::SetPaused_cai(bool paused)
{
    if (Paused_cai() == paused)
        return;
    XPLMCommandRef command = paused ? mPauseOn : mPauseOff;
    mApplying = true;                                 // my own handler lets this one through
    if (command)
        XPLMCommandOnce(command);
    else if (mPauseToggle)
        XPLMCommandOnce(mPauseToggle);
    mApplying = false;
    mAppliedAt = Clock::now();
}

// Makes the mode the session's rule asks for (the host can change it during the session).
void FlightSync::ChooseMode_cai()
{
    const net::FlightMode wanted = mHub.Session_cai().Rules_cai().flight;
    if (mMode && wanted == mModeKind)
        return;
    if (mMode)
        mMode->Stop_cai();
    mModeKind = wanted;
    if (wanted == net::FlightMode::Follow)
        mMode = std::make_unique<FlightFollow>(mHub);
    else
        mMode = std::make_unique<FlightInputs>(mHub);
    mHub.Flight_cai().mode = mMode->Name_cai();
    LOG_INFO("flight: flying together with '{}'", mMode->Name_cai());
    mMode->OnPicChanged_cai(mWasPic);
}

// Pause: the PIC sends, the others follow.
void FlightSync::TickPause_cai(bool iAmPic)
{
    net::NetSession& session = mHub.Session_cai();
    net::FlightStats& stats = mHub.Flight_cai();
    const bool paused = Paused_cai();
    if (iAmPic) {
        session.TakePause_cai();                      // nothing to follow
        if (!mSentPaused || *mSentPaused != paused) {
            session.SendPause_cai(paused);
            mSentPaused = paused;
        }
        stats.picPaused = paused;
        return;
    }
    mSentPaused.reset();
    if (const auto pic = session.TakePause_cai()) {
        mPicPaused = *pic;
        stats.picPaused = *pic;
        if (paused != *pic) {
            LOG_INFO("pause: {} like the PIC", *pic ? "pausing" : "resuming");
            SetPaused_cai(*pic);
        }
        return;
    }
    // Paused / resumed here some other way than the pause commands (they are stopped): back to the PIC's state.
    if (mPicPaused && paused != *mPicPaused &&
        std::chrono::duration<double>(Clock::now() - mAppliedAt).count() > kPauseGraceS) {
        LOG_INFO("pause: {} here, but only the PIC pauses the flight - back to the PIC's state", paused ? "paused" : "resumed");
        mHub.Note_cai(net::SessionEvent::Kind::Declined, "Only the PIC pauses the flight");
        SetPaused_cai(*mPicPaused);
    }
}

// From the flight loop.
void FlightSync::Tick_cai()
{
    net::NetSession& session = mHub.Session_cai();
    if (!session.Active_cai()) {
        if (mRunning)
            Stop_cai("the session ended");
        session.TakeFlightInputs_cai();
        session.TakePause_cai();
        return;
    }
    const bool iAmPic = session.PicId_cai() == session.MyId_cai();
    if (!mRunning) {
        mRunning = true;
        mWasPic = iAmPic;
        mHub.Flight_cai().running = true;
    }
    ChooseMode_cai();
    if (iAmPic != mWasPic) {
        mWasPic = iAmPic;
        mPicPaused.reset();
        mMode->OnPicChanged_cai(iAmPic);
    }
    mHub.Flight_cai().iAmPic = iAmPic;
    mMode->Tick_cai(iAmPic);
    TickPause_cai(iAmPic);
}

// Stops.
void FlightSync::Stop_cai(const char* why)
{
    if (!mRunning && !mMode)
        return;
    if (mMode)
        mMode->Stop_cai();
    mMode.reset();
    mRunning = false;
    mPicPaused.reset();
    mSentPaused.reset();
    mHub.Flight_cai() = {};
    LOG_INFO("flight: stopped ({})", why);
}

} // namespace sc::sync
