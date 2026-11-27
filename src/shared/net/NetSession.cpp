// shared_cockpit - one shared-cockpit session, seen from this simulator (main thread)

#include "shared/net/NetSession.h"

#include <algorithm>

#include "shared/log/Log.h"
#include "shared/store/AnalysisStore.h"
#include "shared/util/Utils.h"

namespace sc::net {

namespace {

// True for the messages that come as a stream (several a second, or every second): not logged one by one.
bool IsStream_cai(MsgType type)
{
    return type == MsgType::People || type == MsgType::Position || type == MsgType::Weather ||
           type == MsgType::FlightInputs || type == MsgType::FlightState || type == MsgType::CopilotInputs;
}

using Clock = std::chrono::steady_clock;

// `seconds` from now.
Clock::time_point After_cai(double seconds)
{
    return Clock::now() + std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0));
}

// True when two names are the same, ignoring case and spaces at the ends.
bool SameName_cai(std::string_view a, std::string_view b)
{
    return Utils::Lower_cai(Utils::Trim_cai(a)) == Utils::Lower_cai(Utils::Trim_cai(b));
}

// "warnings: different .acf version, uses the host's profile" for the log.
std::string FlagsText_cai(std::uint8_t flags)
{
    std::string text;
    auto lmbda_add = [&text](const char* part) {
        if (!text.empty())
            text += ", ";
        text += part;
    };
    if (flags & kFlagAcfVersion)
        lmbda_add("different .acf version");
    if (flags & kFlagHostProfile)
        lmbda_add("uses the host's profile");
    if (flags & kFlagMissingControls)
        lmbda_add("misses some shared controls");
    return text.empty() ? std::string("none") : text;
}

// True when the two lists show the same people, seats and PIC (pings ignored).
bool SamePeople_cai(const std::vector<Person>& a, const std::vector<Person>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || a[i].name != b[i].name || a[i].role != b[i].role || a[i].pic != b[i].pic ||
            a[i].master != b[i].master || a[i].flags != b[i].flags || a[i].seat != b[i].seat)
            return false;
    return true;
}

} // namespace

// The name of a session state.
const char* StateName_cai(SessionState state)
{
    switch (state) {
        case SessionState::Idle:        return "Idle";
        case SessionState::Hosting:     return "Hosting";
        case SessionState::Connecting:  return "Connecting";
        case SessionState::Handshaking: return "Handshaking";
        case SessionState::InSession:   return "InSession";
        case SessionState::Failed:      return "Failed";
        case SessionState::Lost:        return "Lost";
    }
    return "?";
}

NetSession::~NetSession()
{
    if (mState != SessionState::Idle)
        Close_cai(mIsHost ? ByeReason::HostEnded : ByeReason::Leaving);
}

// Who this simulator is.
void NetSession::SetLocal_cai(LocalInfo info)
{
    mLocal = std::move(info);
    LOG_INFO("local: name '{}', aircraft '{}' (stamp {}), profile {} ({} bytes, {} controls)", mLocal.name,
             mLocal.acfName, mLocal.acfStamp, mLocal.profileHash.empty() ? "none" : mLocal.profileHash,
             mLocal.profileText.size(), mLocal.allKeys.size());
}

// ======================================================================================
//  Start / stop
// ======================================================================================

// Opens a session as the host.
bool NetSession::Host_cai(const HostSettings& settings)
{
    ResetForStart_cai();
    mRules = mOwnRules;                                   // the host's own rules decide
    mFailText.clear();
    const std::string name(Utils::Trim_cai(mLocal.name));
    if (name.empty() || name.size() > kMaxNameLength)
        mFailText = fmt::format("your name must be 1 to {} characters", kMaxNameLength);
    else if (mLocal.profileText.empty() || mLocal.profileHash.empty())
        mFailText = "save an analysis with shared controls first (the host's analysis is used by everybody)";
    else if (settings.port == 0)
        mFailText = "the port must be 1 to 65535";
    if (!mFailText.empty()) {
        LOG_WARN("cannot host: {}", mFailText);
        SetState_cai(SessionState::Failed, mFailText);
        return false;
    }
    mHostSettings = settings;
    mHostSettings.maxPeople = std::clamp(settings.maxPeople, 2, kMaxPeople);
    mIsHost = true;
    mMyId = kHostId;
    mPic = kHostId;
    mLinks.clear();
    mPending.clear();
    mPings.clear();
    mQuestions.clear();
    LOG_INFO("host: port {}, password {}, up to {} people, joiners start as {}", mHostSettings.port,
             mHostSettings.password.empty() ? "none" : "set", mHostSettings.maxPeople,
             RoleName_cai(mHostSettings.joinersStartAs));
    mNet.StartHost_cai(mHostSettings.port, kEnetPeers);
    SetState_cai(SessionState::Hosting, fmt::format("hosting on port {}", mHostSettings.port));
    HostRebuildPeople_cai(false);
    mNextPeople = After_cai(kPeopleInterval);
    return true;
}

// Joins a host.
bool NetSession::Join_cai(const JoinSettings& settings)
{
    ResetForStart_cai();
    mRules = mOwnRules;                                   // until the host's arrive (before anything else)
    mRulesFromHost = false;
    mFailText.clear();
    const std::string name(Utils::Trim_cai(mLocal.name));
    const std::string address(Utils::Trim_cai(settings.address));
    if (name.empty() || name.size() > kMaxNameLength)
        mFailText = fmt::format("your name must be 1 to {} characters", kMaxNameLength);
    else if (address.empty())
        mFailText = "enter the host's address (IP or name)";
    else if (settings.port == 0)
        mFailText = "the port must be 1 to 65535";
    if (!mFailText.empty()) {
        LOG_WARN("cannot join: {}", mFailText);
        SetState_cai(SessionState::Failed, mFailText);
        return false;
    }
    mJoinSettings = settings;
    mJoinSettings.address = address;
    mIsHost = false;
    mMyId = kNoId;
    mPic = kNoId;
    mHostLink = kNoLink;
    mByeText.clear();
    mOtherProtocol = false;
    mHostProfile.reset();
    mPeople.clear();
    mQuestions.clear();
    LOG_INFO("join: {}:{} as '{}', password {}", address, settings.port, name, settings.password.empty() ? "none" : "set");
    mNet.StartJoin_cai(address, settings.port);
    mDeadline = After_cai(kConnectTimeout);
    SetState_cai(SessionState::Connecting, fmt::format("connecting to {}:{}", address, settings.port));
    return true;
}

// Before a new Host / Join: a running session is left; an ended one (Failed / Lost, the
// network already stopped) only goes back to Idle, quietly - nobody "left" anything.
void NetSession::ResetForStart_cai()
{
    if (mState == SessionState::Failed || mState == SessionState::Lost) {
        LOG_DEBUG("state {} -> Idle (new start)", StateName_cai(mState));
        mState = SessionState::Idle;
    } else if (mState != SessionState::Idle) {
        Leave_cai();
    }
}

// Leaves or ends the session.
void NetSession::Leave_cai()
{
    if (mState == SessionState::Idle)
        return;
    const bool wasActive = mState != SessionState::Failed && mState != SessionState::Lost;
    if (wasActive)
        LOG_INFO("{}", mIsHost ? "ending the session (host)" : "leaving the session");
    Close_cai(mIsHost ? ByeReason::HostEnded : ByeReason::Leaving);
    SetState_cai(SessionState::Idle, mIsHost ? "session ended" : "left");
}

// Stops the network thread and clears the session data.
void NetSession::Close_cai(ByeReason reason)
{
    if (mIsHost) {
        for (auto& [id, link] : mLinks)
            if (link.step == Link::Step::In)
                Send_cai(link.link, MsgBye{reason, ByeText_cai(reason)});
    } else if (mHostLink != kNoLink && mState == SessionState::InSession) {
        Send_cai(mHostLink, MsgBye{reason, ByeText_cai(reason)});
    }
    mNet.Stop_cai(static_cast<std::uint32_t>(reason));
    mNet.Join_cai();
    mNet.TakeEvents_cai();
    mLinks.clear();
    mPending.clear();
    mPings.clear();
    mQuestions.clear();
    mPeople.clear();
    mLastBegin.clear();
    mHeld.clear();
    mLastValue.clear();
    mSessionValues.clear();
    mValuesIn.clear();
    mSnapshotRequests.clear();
    mPriority.clear();
    mPositions.clear();
    mWeatherIn.reset();
    mInputsIn.reset();
    mPauseIn.reset();
    mPicPaused.reset();
    mCopilotIn.reset();
    mStateIn.reset();
    mLoadIn.reset();
    mPicLoad.reset();
    if (!mHostSeats.empty())
        mSeatsChanged = true;                         // back to my own seats
    mHostSeats.clear();
    mOwnSeat = kNoSeat;
    mCopilotId = kNoId;
    mCommandsIn.clear();
    mRejections.clear();
    mHostLink = kNoLink;
    mHostProfile.reset();
    mMyId = kNoId;
    mPic = kNoId;
}

// Sets the state and reports it.
void NetSession::SetState_cai(SessionState state, const std::string& why)
{
    if (state == mState && why.empty())
        return;
    LOG_INFO("state {} -> {}: {}", StateName_cai(mState), StateName_cai(state), why);
    mState = state;
    if (state == SessionState::Failed || state == SessionState::Lost)
        mFailText = why;
    Event_cai(SessionEvent::Kind::State, why);
}

// Adds an event.
void NetSession::Event_cai(SessionEvent::Kind kind, std::string text)
{
    mEvents.push_back({kind, std::move(text)});
}

// Takes the events.
std::vector<SessionEvent> NetSession::TakeEvents_cai()
{
    std::vector<SessionEvent> out;
    out.swap(mEvents);
    return out;
}

// Sends a message (logged).
void NetSession::Send_cai(LinkId link, const Message& message, bool quiet, bool reliable)
{
    Bytes bytes = EncodeMessage_cai(message);
    if (!quiet)
        LOG_DEBUG("-> {} to link {} ({} bytes)", MsgTypeName_cai(TypeOf_cai(message)), link, bytes.size());
    mNet.Send_cai(link, std::move(bytes), reliable);
}

// ======================================================================================
//  Poll
// ======================================================================================

// Handles the network events and the timers.
void NetSession::Poll_cai()
{
    if (mState == SessionState::Idle)
        return;
    for (NetEvent& event : mNet.TakeEvents_cai()) {
        switch (event.kind) {
            case NetEvent::Kind::Started:
                LOG_DEBUG("network started ({})", event.text);
                if (!mIsHost)
                    mHostLink = event.link;
                break;
            case NetEvent::Kind::StartFailed:
                Close_cai(ByeReason::Leaving);
                SetState_cai(SessionState::Failed, event.text);
                return;
            case NetEvent::Kind::Connected:
                if (mIsHost) {
                    HostOnConnected_cai(event.link, event.text);
                } else if (mState == SessionState::Connecting) {
                    mHostLink = event.link;
                    mDeadline = After_cai(kHandshakeTimeout);
                    SetState_cai(SessionState::Handshaking, fmt::format("connected to {}, introducing myself", event.text));
                    Send_cai(mHostLink, MsgHello{mLocal.name, mLocal.pluginVersion, mLocal.acfName, mLocal.acfStamp,
                                                 mLocal.profileHash});
                }
                break;
            case NetEvent::Kind::Received: {
                std::string error;
                bool otherProtocol = false;
                const std::optional<Message> message =
                    DecodeMessage_cai(event.data.data(), event.data.size(), error, otherProtocol);
                if (mIsHost) {
                    auto found = mLinks.find(event.link);
                    if (found == mLinks.end() || found->second.step == Link::Step::Leaving)
                        break;
                    if (!message) {
                        LOG_WARN("link {}: unreadable message: {}", event.link, error);
                        if (otherProtocol)
                            HostRefuse_cai(found->second, RefuseReason::ProtocolVersion, error);
                        else if (found->second.step == Link::Step::In)
                            LOG_WARN("link {} ({}): message ignored", event.link, found->second.person.name);
                        else
                            HostRefuse_cai(found->second, RefuseReason::ProtocolVersion, error);
                        break;
                    }
                    if (!IsStream_cai(TypeOf_cai(*message)))
                        LOG_DEBUG("<- {} from link {}", MsgTypeName_cai(TypeOf_cai(*message)), event.link);
                    HostOnReceived_cai(found->second, *message);
                } else {
                    if (!message) {
                        LOG_WARN("unreadable message from the host: {}", error);
                        mOtherProtocol = mOtherProtocol || otherProtocol;
                        if (otherProtocol && mState == SessionState::Handshaking) {
                            Close_cai(ByeReason::ProtocolError);
                            SetState_cai(SessionState::Failed,
                                         fmt::format("the host uses another Shared Cockpit version ({})", error));
                            return;
                        }
                        break;
                    }
                    if (!IsStream_cai(TypeOf_cai(*message)))      // streams: not logged one by one
                        LOG_DEBUG("<- {} from the host", MsgTypeName_cai(TypeOf_cai(*message)));
                    JoinOnReceived_cai(*message);
                    if (mState == SessionState::Failed || mState == SessionState::Lost || mState == SessionState::Idle)
                        return;
                }
                break;
            }
            case NetEvent::Kind::Disconnected:
                if (mIsHost) {
                    HostOnDisconnected_cai(event.link, event.code);
                } else {
                    JoinOnDisconnected_cai(event.code);
                    return;
                }
                break;
            case NetEvent::Kind::Ping:
                if (mIsHost) {
                    if (const auto found = mLinks.find(event.link); found != mLinks.end() && found->second.step == Link::Step::In)
                        mPings[found->second.person.id] = static_cast<std::uint16_t>(std::min<std::uint32_t>(event.code, 0xFFFF));
                }
                break;
            case NetEvent::Kind::Stopped:
                LOG_DEBUG("network thread stopped");
                break;
        }
    }
    if (mIsHost)
        HostTimers_cai();
    else
        JoinTimers_cai();
}

// ======================================================================================
//  Host side
// ======================================================================================

// A new link: wait for its Hello.
void NetSession::HostOnConnected_cai(LinkId link, const std::string& address)
{
    Link entry;
    entry.link = link;
    entry.address = address;
    entry.deadline = After_cai(kHandshakeTimeout);
    mLinks[link] = entry;
    LOG_INFO("link {} connected from {}: waiting for its Hello ({} s)", link, address, kHandshakeTimeout);
}

// A message from a joiner, by handshake step.
void NetSession::HostOnReceived_cai(Link& link, const Message& message)
{
    const MsgType type = TypeOf_cai(message);
    switch (link.step) {
        case Link::Step::Hello: {
            const auto* hello = std::get_if<MsgHello>(&message);
            if (!hello) {
                HostRefuse_cai(link, RefuseReason::ProtocolVersion, fmt::format("expected Hello, got {}", MsgTypeName_cai(type)));
                return;
            }
            link.hello = *hello;
            const std::string name(Utils::Trim_cai(hello->name));
            LOG_INFO("link {}: Hello from '{}' (plugin {}, aircraft '{}', stamp {}, profile {})", link.link, name,
                     hello->pluginVersion, hello->acfName, hello->acfStamp,
                     hello->profileHash.empty() ? "none" : hello->profileHash);
            if (name.empty() || name.size() > kMaxNameLength) {
                HostRefuse_cai(link, RefuseReason::BadName, fmt::format("the name must be 1 to {} characters", kMaxNameLength));
                return;
            }
            bool used = SameName_cai(name, mLocal.name);
            int people = 1;
            for (const auto& [otherLink, other] : mLinks) {
                if (otherLink == link.link || other.step == Link::Step::Leaving || other.step == Link::Step::Hello)
                    continue;
                ++people;
                used = used || SameName_cai(name, other.hello.name);
            }
            if (people >= mHostSettings.maxPeople) {
                HostRefuse_cai(link, RefuseReason::SessionFull,
                               fmt::format("the session is full ({} people)", mHostSettings.maxPeople));
                return;
            }
            if (used) {
                HostRefuse_cai(link, RefuseReason::NameInUse, fmt::format("'{}' is already in the session", name));
                return;
            }
            if (!SameName_cai(hello->acfName, mLocal.acfName)) {
                HostRefuse_cai(link, RefuseReason::OtherAircraft,
                               fmt::format("the host flies '{}', you fly '{}'", mLocal.acfName, hello->acfName));
                return;
            }
            if (!mHostSettings.password.empty()) {
                link.nonce = MakeNonce_cai();
                link.step = Link::Step::Proof;
                link.deadline = After_cai(kHandshakeTimeout);
                LOG_INFO("link {} ('{}'): password needed, Challenge sent", link.link, name);
                Send_cai(link.link, MsgChallenge{link.nonce});
                return;
            }
            HostWelcome_cai(link);
            return;
        }
        case Link::Step::Proof: {
            const auto* proof = std::get_if<MsgProof>(&message);
            if (!proof) {
                HostRefuse_cai(link, RefuseReason::ProtocolVersion, fmt::format("expected Proof, got {}", MsgTypeName_cai(type)));
                return;
            }
            if (proof->proof != PasswordProof_cai(mHostSettings.password, link.nonce)) {
                LOG_WARN("link {} ('{}'): wrong password", link.link, link.hello.name);
                HostRefuse_cai(link, RefuseReason::WrongPassword, "wrong password");
                return;
            }
            LOG_INFO("link {} ('{}'): password correct", link.link, link.hello.name);
            HostWelcome_cai(link);
            return;
        }
        case Link::Step::ProfileAck: {
            const auto* ack = std::get_if<MsgProfileAck>(&message);
            if (!ack) {
                LOG_WARN("link {}: expected ProfileAck, got {} (ignored)", link.link, MsgTypeName_cai(type));
                return;
            }
            if (!ack->ok) {
                LOG_WARN("link {} ('{}'): could not use the host's profile: {}", link.link, link.hello.name, ack->text);
                Send_cai(link.link, MsgBye{ByeReason::ProtocolError, "your plugin could not read the host's analysis"});
                link.step = Link::Step::Leaving;
                mNet.Disconnect_cai(link.link, static_cast<std::uint32_t>(ByeReason::ProtocolError));
                return;
            }
            HostAdmit_cai(link, *ack);
            return;
        }
        case Link::Step::In: {
            const std::uint8_t id = link.person.id;
            if (std::get_if<MsgControlRequest>(&message)) {
                HostOnRequest_cai(id);
            } else if (const auto* offer = std::get_if<MsgControlOffer>(&message)) {
                HostOnOffer_cai(id, offer->toId);
            } else if (const auto* answer = std::get_if<MsgControlAnswer>(&message)) {
                HostOnAnswer_cai(id, answer->askerId, answer->accept);
            } else if (const auto* notice = std::get_if<MsgNotice>(&message)) {
                HostOnNotice_cai(id, notice->text);
            } else if (const auto* command = std::get_if<MsgCommand>(&message)) {
                HostOnCommand_cai(id, command->index, command->phase);
            } else if (const auto* values = std::get_if<MsgValues>(&message)) {
                HostOnValues_cai(id, values->values);
            } else if (const auto* position = std::get_if<MsgPosition>(&message)) {
                mPositions[id] = {position->position, Clock::now()};
                const MsgPosition out{id, position->position};
                for (const auto& [linkId, other] : mLinks)
                    if (other.step == Link::Step::In && other.person.id != id)
                        Send_cai(other.link, out, true);
            } else if (const auto* inputs = std::get_if<MsgFlightInputs>(&message)) {
                if (id == PicId_cai()) {                      // only the PIC's hands fly (not a late packet after a handover)
                    mInputsIn = inputs->inputs;               // the host flies with them too
                    const MsgFlightInputs out{id, inputs->sequence, inputs->inputs};
                    for (const auto& [linkId, other] : mLinks)
                        if (other.step == Link::Step::In && other.person.id != id)
                            Send_cai(other.link, out, true, false);
                }
            } else if (const auto* state = std::get_if<MsgFlightState>(&message)) {
                if (id == PicId_cai()) {                      // only the PIC's aircraft leads
                    mStateIn = StateIn{state->state, state->sequence, state->timeMs, Clock::now()};
                    const MsgFlightState out{id, state->sequence, state->timeMs, state->state};
                    for (const auto& [linkId, other] : mLinks)
                        if (other.step == Link::Step::In && other.person.id != id)
                            Send_cai(other.link, out, true, false);
                }
            } else if (const auto* load = std::get_if<MsgLoad>(&message)) {
                if (id == PicId_cai()) {
                    mLoadIn = load->load;
                    mPicLoad = load->load;                    // for new joiners
                    const MsgLoad out{id, load->load};
                    for (const auto& [linkId, other] : mLinks)
                        if (other.step == Link::Step::In && other.person.id != id)
                            Send_cai(other.link, out, true);
                }
            } else if (const auto* copilot = std::get_if<MsgCopilotInputs>(&message)) {
                if (HostCopilotSeat_cai(id)) {
                    mCopilotIn = CopilotIn{id, copilot->inputs};
                    const MsgCopilotInputs out{id, copilot->inputs};
                    for (const auto& [linkId, other] : mLinks)
                        if (other.step == Link::Step::In && other.person.id != id)
                            Send_cai(other.link, out, true, false);
                }
            } else if (const auto* pause = std::get_if<MsgPause>(&message)) {
                if (id == PicId_cai()) {
                    LOG_INFO("pause: the PIC '{}' {}", link.person.name, pause->paused ? "paused" : "resumed");
                    mPicPaused = pause->paused;
                    mPauseIn = pause->paused;
                    const MsgPause out{id, pause->paused};
                    for (const auto& [linkId, other] : mLinks)
                        if (other.step == Link::Step::In && other.person.id != id)
                            Send_cai(other.link, out);
                }
            } else if (const auto* choice = std::get_if<MsgSeatChoice>(&message)) {
                const std::uint8_t seat = choice->seat < mOwnSeats.size() ? choice->seat : kNoSeat;
                if (link.person.seat != seat) {
                    link.person.seat = seat;
                    LOG_INFO("'{}' sits in {}", link.person.name, seat == kNoSeat ? std::string("no seat") : mOwnSeats[seat].name);
                    HostRebuildPeople_cai(true);
                }
            } else if (std::get_if<MsgSnapshotRequest>(&message)) {
                LOG_INFO("'{}' asks for the snapshot again (its aircraft was reloaded)", link.person.name);
                if (std::find(mSnapshotRequests.begin(), mSnapshotRequests.end(), id) == mSnapshotRequests.end())
                    mSnapshotRequests.push_back(id);
            } else if (const auto* bye = std::get_if<MsgBye>(&message)) {
                LOG_INFO("'{}' says goodbye: {}", link.person.name, bye->text);
            } else {
                LOG_WARN("'{}' sent {} (not expected from a joiner, ignored)", link.person.name, MsgTypeName_cai(type));
            }
            return;
        }
        case Link::Step::Leaving:
            return;
    }
}

// Refuses a joiner: Refused, then the link is closed once it was sent.
void NetSession::HostRefuse_cai(Link& link, RefuseReason reason, const std::string& text)
{
    LOG_INFO("link {} ('{}', {}) refused: {}{}", link.link, link.hello.name, link.address, RefuseText_cai(reason),
             text == RefuseText_cai(reason) ? std::string() : " - " + text);
    Send_cai(link.link, MsgRefused{reason, text});
    link.step = Link::Step::Leaving;
    mNet.Disconnect_cai(link.link, static_cast<std::uint32_t>(ByeReason::Refused));
    Event_cai(SessionEvent::Kind::Warning,
              fmt::format("refused '{}' ({}): {}", link.hello.name, link.address, text));
}

// Welcome (id, seat, aircraft), plus the host's profile when the joiner's differs.
void NetSession::HostWelcome_cai(Link& link)
{
    link.person.id = HostFreeId_cai();
    link.person.name = std::string(Utils::Trim_cai(link.hello.name));
    link.person.role = mHostSettings.joinersStartAs;
    link.step = Link::Step::ProfileAck;
    link.deadline = After_cai(kProfileTimeout);
    LOG_INFO("link {}: welcome '{}' as id {}, {}", link.link, link.person.name, link.person.id,
             RoleName_cai(link.person.role));
    Send_cai(link.link, MsgWelcome{link.person.id, link.person.role, mLocal.name, mLocal.acfName, mLocal.acfStamp,
                                   mLocal.profileHash});
    if (link.hello.profileHash != mLocal.profileHash) {
        LOG_INFO("link {}: profile differs (theirs {}, mine {}): sending mine ({} bytes)", link.link,
                 link.hello.profileHash.empty() ? "none" : link.hello.profileHash, mLocal.profileHash,
                 mLocal.profileText.size());
        Send_cai(link.link, MsgProfile{mLocal.profileHash, mLocal.profileText});
    } else {
        LOG_INFO("link {}: same profile {}", link.link, mLocal.profileHash);
    }
}

// The joiner acknowledged the profile: it is in.
void NetSession::HostAdmit_cai(Link& link, const MsgProfileAck& ack)
{
    std::uint8_t flags = kFlagNone;
    if (link.hello.acfStamp != mLocal.acfStamp)
        flags |= kFlagAcfVersion;
    if (link.hello.profileHash != mLocal.profileHash)
        flags |= kFlagHostProfile;
    if (ack.missing > 0)
        flags |= kFlagMissingControls;
    link.person.flags = flags;
    link.step = Link::Step::In;
    LOG_INFO("'{}' joined: id {}, {}, from {}; warnings: {}{}", link.person.name, link.person.id,
             RoleName_cai(link.person.role), link.address, FlagsText_cai(flags),
             ack.missing > 0 ? fmt::format(" ({} shared controls missing)", ack.missing) : std::string());
    mPriority.push_back(link.person.id);
    mSnapshotRequests.push_back(link.person.id);
    Send_cai(link.link, MsgRules{mRules});                 // the rules first: they decide how the rest works
    if (mPicPaused)
        Send_cai(link.link, MsgPause{PicId_cai(), *mPicPaused});
    if (mPicLoad && mRules.shareLoad)
        Send_cai(link.link, MsgLoad{PicId_cai(), *mPicLoad}, true);
    Send_cai(link.link, MsgSeats{mOwnSeats});
    link.person.seat = kNoSeat;
    HostRebuildPeople_cai(true);
    HostSendPriority_cai();
    const std::string text = fmt::format("{} joined as {}", link.person.name, RoleName_cai(link.person.role));
    HostBroadcast_cai(MsgNotice{text});
    Event_cai(SessionEvent::Kind::Notice, text);
    if (flags & kFlagAcfVersion)
        Event_cai(SessionEvent::Kind::Warning,
                  fmt::format("{} has another version of the aircraft (.acf stamp differs)", link.person.name));
    if (flags & kFlagMissingControls)
        Event_cai(SessionEvent::Kind::Warning,
                  fmt::format("{}'s aircraft misses {} of the shared controls", link.person.name, ack.missing));
}

// A link closed.
void NetSession::HostOnDisconnected_cai(LinkId linkId, std::uint32_t code)
{
    const auto found = mLinks.find(linkId);
    if (found == mLinks.end())
        return;
    Link& link = found->second;
    if (link.step == Link::Step::In) {
        const std::string why = code == 0 ? std::string("connection lost") : std::string(ByeText_cai(static_cast<ByeReason>(code)));
        HostRemove_cai(link, why);
    } else if (link.step != Link::Step::Leaving) {
        LOG_INFO("link {} ({}) closed during the handshake: {}", linkId, link.address,
                 code == 0 ? "connection lost" : ByeText_cai(static_cast<ByeReason>(code)));
    }
    mLinks.erase(found);
}

// Takes a joiner out of the session (left, lost, kicked).
void NetSession::HostRemove_cai(Link& link, const std::string& why)
{
    const std::uint8_t id = link.person.id;
    const std::string name = link.person.name;
    link.step = Link::Step::Leaving;
    LOG_INFO("'{}' (id {}) is out: {}", name, id, why);
    HostReleaseHeld_cai(id);
    std::erase(mPriority, id);
    std::erase(mSnapshotRequests, id);
    mPositions.erase(id);
    for (auto it = mLastValue.begin(); it != mLastValue.end();)
        it = it->second.author == id ? mLastValue.erase(it) : std::next(it);
    HostSendPriority_cai();
    mPings.erase(id);
    auto lmbda_pendingOf = [id](const Pending& p) { return p.fromId == id || p.toId == id; };
    auto lmbda_questionOf = [id](const Question& q) { return q.fromId == id; };
    std::erase_if(mPending, lmbda_pendingOf);
    std::erase_if(mQuestions, lmbda_questionOf);
    const std::string text = fmt::format("{} left ({})", name, why);
    HostBroadcast_cai(MsgNotice{text});
    Event_cai(SessionEvent::Kind::Notice, text);
    if (mPic == id)
        HostSetPic_cai(kHostId, fmt::format("{} left, control returns to the host", name));
    else
        HostRebuildPeople_cai(true);
}

// Someone asks the PIC for control.
void NetSession::HostOnRequest_cai(std::uint8_t fromId)
{
    const Person* from = Find_cai(fromId);
    if (!from)
        return;
    if (fromId == mPic) {
        HostDeclined_cai(fromId, "you already have control");
        return;
    }
    if (from->role == Role::Observer) {
        LOG_INFO("control request from '{}' refused: observers cannot take control", from->name);
        HostDeclined_cai(fromId, "observers cannot take control");
        return;
    }
    for (const Pending& p : mPending)
        if (p.fromId == fromId) {
            HostDeclined_cai(fromId, "you already asked, waiting for the answer");
            return;
        }
    Pending pending{Question::Kind::Request, fromId, mPic, After_cai(kRequestTimeout)};
    LOG_INFO("control: '{}' asks '{}' for control", from->name, NameOf_cai(mPic));
    mPending.push_back(pending);
    HostDeliverQuestion_cai(pending);
}

// The PIC offers control to someone.
void NetSession::HostOnOffer_cai(std::uint8_t fromId, std::uint8_t toId)
{
    const Person* to = Find_cai(toId);
    if (fromId != mPic) {
        HostDeclined_cai(fromId, "only the pilot in command can offer control");
        return;
    }
    if (!to || toId == fromId) {
        HostDeclined_cai(fromId, "no such person");
        return;
    }
    if (to->role == Role::Observer) {
        HostDeclined_cai(fromId, fmt::format("{} is an observer", to->name));
        return;
    }
    auto lmbda_sameOffer = [fromId, toId](const Pending& p) { return p.fromId == fromId && p.toId == toId; };
    std::erase_if(mPending, lmbda_sameOffer);
    Pending pending{Question::Kind::Offer, fromId, toId, After_cai(kRequestTimeout)};
    LOG_INFO("control: '{}' offers control to '{}'", NameOf_cai(fromId), to->name);
    mPending.push_back(pending);
    HostDeliverQuestion_cai(pending);
}

// Someone answered a request or an offer.
void NetSession::HostOnAnswer_cai(std::uint8_t answerId, std::uint8_t askerId, bool accept)
{
    auto lmbda_isAnswered = [=](const Pending& p) { return p.toId == answerId && p.fromId == askerId; };
    const auto found = std::find_if(mPending.begin(), mPending.end(), lmbda_isAnswered);
    if (found == mPending.end()) {
        LOG_WARN("answer from '{}' to '{}' ignored: no open question (expired?)", NameOf_cai(answerId), NameOf_cai(askerId));
        return;
    }
    const Pending pending = *found;
    mPending.erase(found);
    LOG_INFO("control: '{}' answers '{}' ({}): {}", NameOf_cai(answerId), NameOf_cai(askerId),
             pending.kind == Question::Kind::Request ? "request" : "offer", accept ? "yes" : "no");
    if (!accept) {
        HostDeclined_cai(askerId, fmt::format("{} said no", NameOf_cai(answerId)));
        return;
    }
    if (pending.kind == Question::Kind::Request) {
        if (mPic != answerId) {
            HostDeclined_cai(askerId, "control changed meanwhile");
            return;
        }
        HostSetPic_cai(askerId, fmt::format("{} gave control to {}", NameOf_cai(answerId), NameOf_cai(askerId)));
    } else {
        if (mPic != askerId) {
            HostDeclined_cai(answerId, "control changed meanwhile");
            return;
        }
        HostSetPic_cai(answerId, fmt::format("{} accepted control from {}", NameOf_cai(answerId), NameOf_cai(askerId)));
    }
}

// A notice from someone goes to everybody.
void NetSession::HostOnNotice_cai(std::uint8_t fromId, const std::string& text)
{
    std::string body(Utils::Trim_cai(text));
    if (body.empty())
        return;
    if (body.size() > 200)
        body.resize(200);
    const std::string line = fmt::format("{}: {}", NameOf_cai(fromId), body);
    LOG_INFO("notice {}", line);
    HostBroadcast_cai(MsgNotice{line});
    Event_cai(SessionEvent::Kind::Notice, line);
}

// A new PIC: tells everybody.
void NetSession::HostSetPic_cai(std::uint8_t newPic, const std::string& reason)
{
    const std::uint8_t oldPic = mPic;
    mPic = newPic;
    LOG_INFO("control: PIC '{}' -> '{}' ({})", NameOf_cai(oldPic), NameOf_cai(newPic), reason);
    mPending.clear();          // every open question was about the old PIC
    mQuestions.clear();
    HostBroadcast_cai(MsgControlChanged{newPic, oldPic, reason});
    HostRebuildPeople_cai(true);
    Event_cai(SessionEvent::Kind::ControlChanged, fmt::format("{} has control ({})", NameOf_cai(newPic), reason));
}

// Gives a question to the person who must answer it.
void NetSession::HostDeliverQuestion_cai(const Pending& pending)
{
    if (pending.toId == kHostId) {
        AddQuestion_cai(pending.kind, pending.fromId);
        return;
    }
    Link* link = HostLinkById_cai(pending.toId);
    if (!link)
        return;
    if (pending.kind == Question::Kind::Request)
        Send_cai(link->link, MsgControlRequest{pending.fromId});
    else
        Send_cai(link->link, MsgControlOffer{pending.fromId, pending.toId});
}

// Tells the asker its request / offer did not go through.
void NetSession::HostDeclined_cai(std::uint8_t askerId, const std::string& text)
{
    LOG_INFO("control: to '{}': {}", NameOf_cai(askerId), text);
    if (askerId == kHostId) {
        Event_cai(SessionEvent::Kind::Declined, text);
        return;
    }
    if (Link* link = HostLinkById_cai(askerId)) {
        Send_cai(link->link, MsgControlAnswer{askerId, kNoId, false});
        Send_cai(link->link, MsgNotice{text});
    }
}

// Sends a message to every joiner in the session.
void NetSession::HostBroadcast_cai(const Message& message)
{
    for (const auto& [id, link] : mLinks)
        if (link.step == Link::Step::In)
            Send_cai(link.link, message);
}

// The people list from the links (and sends it).
void NetSession::HostRebuildPeople_cai(bool send)
{
    std::vector<Person> people;
    Person host;
    host.id = kHostId;
    host.name = mLocal.name;
    host.role = Role::Crew;
    host.master = true;
    host.pic = mPic == kHostId;
    host.seat = mOwnSeat;
    people.push_back(host);
    for (const auto& [id, link] : mLinks) {
        if (link.step != Link::Step::In)
            continue;
        Person person = link.person;
        person.master = false;
        person.pic = mPic == person.id;
        const auto ping = mPings.find(person.id);
        person.pingMs = ping == mPings.end() ? 0 : ping->second;
        people.push_back(person);
    }
    auto lmbda_byId = [](const Person& a, const Person& b) { return a.id < b.id; };
    std::sort(people.begin(), people.end(), lmbda_byId);
    const bool changed = !SamePeople_cai(people, mPeople);
    mPeople = std::move(people);
    if (send) {
        // Logged only when something changed (not the 2 s heartbeat with new pings).
        const MsgPeople message{mPeople};
        for (const auto& [id, link] : mLinks)
            if (link.step == Link::Step::In)
                Send_cai(link.link, message, !changed);
    }
    if (changed)
        Event_cai(SessionEvent::Kind::People, {});
}

// Handshake timeouts, expired questions, the People heartbeat.
void NetSession::HostTimers_cai()
{
    const auto now = Clock::now();
    for (auto& [id, link] : mLinks) {
        if (link.step == Link::Step::In || link.step == Link::Step::Leaving || now < link.deadline)
            continue;
        LOG_WARN("link {} ({}): handshake timeout at step {}", link.link, link.address, static_cast<int>(link.step));
        link.step = Link::Step::Leaving;
        mNet.Disconnect_cai(link.link, static_cast<std::uint32_t>(ByeReason::Timeout));
    }
    for (auto it = mPending.begin(); it != mPending.end();) {
        if (now < it->until) {
            ++it;
            continue;
        }
        const Pending pending = *it;
        it = mPending.erase(it);
        LOG_INFO("control: '{}' did not answer '{}' in {} s", NameOf_cai(pending.toId), NameOf_cai(pending.fromId), kRequestTimeout);
        HostDeclined_cai(pending.fromId, fmt::format("{} did not answer", NameOf_cai(pending.toId)));
    }
    ExpireQuestions_cai();
    if (now >= mNextPeople) {
        mNextPeople = After_cai(kPeopleInterval);
        HostRebuildPeople_cai(true);
    }
}

// The link of a joiner in the session.
NetSession::Link* NetSession::HostLinkById_cai(std::uint8_t id)
{
    for (auto& [linkId, link] : mLinks)
        if (link.step == Link::Step::In && link.person.id == id)
            return &link;
    return nullptr;
}

// The lowest free id for a new joiner (1..).
std::uint8_t NetSession::HostFreeId_cai() const
{
    for (std::uint8_t id = 1; id < 250; ++id) {
        bool used = false;
        for (const auto& [linkId, link] : mLinks)
            used = used || (link.step != Link::Step::Hello && link.step != Link::Step::Proof &&
                            link.step != Link::Step::Leaving && link.person.id == id);
        if (!used)
            return id;
    }
    return kNoId;
}

// ======================================================================================
//  Joiner side
// ======================================================================================

// A message from the host.
void NetSession::JoinOnReceived_cai(const Message& message)
{
    if (const auto* challenge = std::get_if<MsgChallenge>(&message)) {
        LOG_INFO("the host wants a password: sending the proof");
        Send_cai(mHostLink, MsgProof{PasswordProof_cai(mJoinSettings.password, challenge->nonce)});
    } else if (const auto* welcome = std::get_if<MsgWelcome>(&message)) {
        mMyId = welcome->yourId;
        LOG_INFO("welcomed by '{}': id {}, {}; host aircraft '{}' (stamp {}), host profile {}", welcome->hostName,
                 welcome->yourId, RoleName_cai(welcome->yourRole), welcome->acfName, welcome->acfStamp,
                 welcome->profileHash);
        if (welcome->acfStamp != mLocal.acfStamp) {
            LOG_WARN("the host's .acf stamp {} differs from mine {}", welcome->acfStamp, mLocal.acfStamp);
            Event_cai(SessionEvent::Kind::Warning,
                      "the host has another version of this aircraft: some controls may not match");
        }
        if (!welcome->profileHash.empty() && welcome->profileHash == mLocal.profileHash) {
            LOG_INFO("same profile as the host: using my own analysis");
            Send_cai(mHostLink, MsgProfileAck{true, 0, "same profile"});
            SetState_cai(SessionState::InSession, fmt::format("in the session of {}", welcome->hostName));
        } else {
            mDeadline = After_cai(kProfileTimeout);
            LOG_INFO("waiting for the host's profile");
        }
    } else if (const auto* profile = std::get_if<MsgProfile>(&message)) {
        JoinCheckProfile_cai(*profile);
    } else if (const auto* refused = std::get_if<MsgRefused>(&message)) {
        LOG_WARN("refused by the host: {} ({})", refused->text, RefuseText_cai(refused->reason));
        const std::string text = fmt::format("refused: {}", refused->text.empty() ? RefuseText_cai(refused->reason) : refused->text);
        Close_cai(ByeReason::Refused);
        SetState_cai(SessionState::Failed, text);
    } else if (const auto* people = std::get_if<MsgPeople>(&message)) {
        for (auto it = mPositions.begin(); it != mPositions.end();) {
            auto lmbda_sameId = [&it](const Person& p) { return p.id == it->first; };
            it = std::none_of(people->people.begin(), people->people.end(), lmbda_sameId) ? mPositions.erase(it) : std::next(it);
        }
        const bool changed = !SamePeople_cai(people->people, mPeople);
        mPeople = people->people;
        mPic = PicId_cai();
        if (changed) {
            std::string list;
            for (const Person& p : mPeople)
                list += fmt::format("{}#{} {} {}{}", list.empty() ? "" : ", ", p.id, p.name, RoleName_cai(p.role),
                                    p.pic ? " PIC" : "");
            LOG_INFO("people: {}", list);
            Event_cai(SessionEvent::Kind::People, {});
        }
    } else if (const auto* request = std::get_if<MsgControlRequest>(&message)) {
        AddQuestion_cai(Question::Kind::Request, request->fromId);
    } else if (const auto* offer = std::get_if<MsgControlOffer>(&message)) {
        AddQuestion_cai(Question::Kind::Offer, offer->fromId);
    } else if (const auto* answer = std::get_if<MsgControlAnswer>(&message)) {
        if (!answer->accept)
            LOG_INFO("control: my request / offer did not go through");
    } else if (const auto* changed = std::get_if<MsgControlChanged>(&message)) {
        mQuestions.clear();
        mPic = changed->newPic;
        LOG_INFO("control: PIC '{}' -> '{}' ({})", NameOf_cai(changed->oldPic), NameOf_cai(changed->newPic), changed->reason);
        Event_cai(SessionEvent::Kind::ControlChanged,
                  changed->newPic == mMyId ? fmt::format("you have control ({})", changed->reason)
                                           : fmt::format("{} has control ({})", NameOf_cai(changed->newPic), changed->reason));
    } else if (const auto* command = std::get_if<MsgCommand>(&message)) {
        LOG_DEBUG("command {} {} by '{}'", CommandText_cai(command->index),
                  command->phase == CommandPhase::Begin ? "Begin" : "End", NameOf_cai(command->author));
        mCommandsIn.push_back({command->index, command->phase, command->author});
    } else if (const auto* values = std::get_if<MsgValues>(&message)) {
        LOG_DEBUG("values by '{}': {}", NameOf_cai(values->author), values->values.size());
        mValuesIn.push_back({RemoteValues::Kind::Change, values->author, {}, values->values});
    } else if (const auto* correction = std::get_if<MsgCorrection>(&message)) {
        LOG_INFO("{} of my changes not shared ({}): back to the session's values", correction->values.size(), correction->text);
        mValuesIn.push_back({RemoteValues::Kind::Correction, correction->winner, correction->text, correction->values});
    } else if (const auto* snapshot = std::get_if<MsgSnapshot>(&message)) {
        LOG_INFO("snapshot part {} of {}: {} values", snapshot->part + 1, snapshot->parts, snapshot->values.size());
        mValuesIn.push_back({RemoteValues::Kind::Snapshot, kHostId, {}, snapshot->values});
    } else if (const auto* rules = std::get_if<MsgRules>(&message)) {
        const bool rulesChanged = mRulesFromHost && !(rules->rules == mRules);
        mRules = rules->rules;
        mRulesFromHost = true;
        LOG_INFO("session rules from the host: {}", RulesText_cai(mRules));
        if (rulesChanged)
            Event_cai(SessionEvent::Kind::Notice, "The host changed the session settings");
    } else if (const auto* priority = std::get_if<MsgPriority>(&message)) {
        mPriority = priority->order;
        std::string list;
        for (std::size_t i = 0; i < mPriority.size(); ++i)
            list += fmt::format("{}{} {}", list.empty() ? "" : ", ", i + 2, NameOf_cai(mPriority[i]));
        LOG_INFO("priority: 1 {}{}{}", NameOf_cai(kHostId), list.empty() ? "" : ", ", list);
        Event_cai(SessionEvent::Kind::People, {});
    } else if (const auto* position = std::get_if<MsgPosition>(&message)) {
        if (position->id != mMyId && Find_cai(position->id))
            mPositions[position->id] = {position->position, Clock::now()};
    } else if (const auto* weather = std::get_if<MsgWeather>(&message)) {
        mWeatherIn = weather->values;
    } else if (const auto* inputs = std::get_if<MsgFlightInputs>(&message)) {
        if (inputs->id == PicId_cai() && inputs->id != mMyId)
            mInputsIn = inputs->inputs;
    } else if (const auto* state = std::get_if<MsgFlightState>(&message)) {
        if (state->id == PicId_cai() && state->id != mMyId)
            mStateIn = StateIn{state->state, state->sequence, state->timeMs, Clock::now()};
    } else if (const auto* load = std::get_if<MsgLoad>(&message)) {
        if (load->id != mMyId)
            mLoadIn = load->load;
    } else if (const auto* seats = std::get_if<MsgSeats>(&message)) {
        if (seats->seats != mHostSeats) {
            mHostSeats = seats->seats;
            mOwnSeat = kNoSeat;
            mSeatsChanged = true;
            LOG_INFO("the host's seats: {}", mHostSeats.size());
        }
    } else if (const auto* copilot = std::get_if<MsgCopilotInputs>(&message)) {
        if (copilot->id != mMyId && mRules.copilotAxes)
            mCopilotIn = CopilotIn{copilot->id, copilot->inputs};
    } else if (const auto* pause = std::get_if<MsgPause>(&message)) {
        if (pause->id != mMyId) {
            LOG_INFO("pause: the PIC {} the flight", pause->paused ? "paused" : "resumed");
            mPauseIn = pause->paused;
        }
    } else if (const auto* reject = std::get_if<MsgReject>(&message)) {
        LOG_INFO("my command {} was not shared: {}", CommandText_cai(reject->index), reject->text);
        mRejections.push_back({reject->index, reject->winner, reject->text});
    } else if (const auto* notice = std::get_if<MsgNotice>(&message)) {
        LOG_INFO("notice: {}", notice->text);
        Event_cai(SessionEvent::Kind::Notice, notice->text);
    } else if (const auto* bye = std::get_if<MsgBye>(&message)) {
        mByeText = bye->text.empty() ? ByeText_cai(bye->reason) : bye->text;
        LOG_INFO("the host says goodbye: {}", mByeText);
    } else {
        LOG_WARN("{} from the host was not expected (ignored)", MsgTypeName_cai(TypeOf_cai(message)));
    }
}

// The host's profile: check it, keep it for the session (in memory only).
void NetSession::JoinCheckProfile_cai(const MsgProfile& profile)
{
    auto store = store::MakeAnalysisStore_cai();
    analysis::AnalysisData data;
    std::string error;
    if (!store->FromText_cai(profile.text, data, error)) {
        LOG_ERROR("the host's profile cannot be read: {}", error);
        Send_cai(mHostLink, MsgProfileAck{false, 0, error});
        return;
    }
    const std::string hash = analysis::ProfileHash_cai(data);
    if (hash != profile.hash)
        LOG_WARN("the host's profile hash {} does not match its content ({})", profile.hash, hash);
    // Without an analysis of its own the joiner cannot tell what is missing: 0, and a note.
    const bool ownAnalysis = !mLocal.allKeys.empty();
    const std::size_t missing = ownAnalysis ? analysis::CountMissing_cai(analysis::SharedKeys_cai(data), mLocal.allKeys) : 0;
    if (!ownAnalysis)
        Event_cai(SessionEvent::Kind::Warning,
                  "you have no analysis of this aircraft: the host's is used (analyse it to see what may be missing)");
    LOG_INFO("host profile: {} bytes, {} shared controls, {} not in my aircraft; used for this session only "
             "(my own file is not changed)", profile.text.size(), data.SharedCount_cai(), missing);
    mHostProfile = std::move(data);
    Send_cai(mHostLink, MsgProfileAck{true, static_cast<std::uint16_t>(std::min<std::size_t>(missing, 0xFFFF)), "ok"});
    SetState_cai(SessionState::InSession, "in the session, using the host's analysis");
    if (missing > 0)
        Event_cai(SessionEvent::Kind::Warning,
                  fmt::format("{} of the host's shared controls are not in your aircraft", missing));
}

// The link to the host closed.
void NetSession::JoinOnDisconnected_cai(std::uint32_t code)
{
    const SessionState state = mState;
    std::string why;
    if (state == SessionState::Connecting)
        why = "no answer from the host (address, port, firewall / port forwarding, or the host is not hosting)";
    else if (state == SessionState::Handshaking)
        why = mOtherProtocol ? std::string("the host uses another Shared Cockpit version")
                             : (code == 0 ? std::string("the host did not answer the handshake")
                                          : fmt::format("the host closed the connection: {}", ByeText_cai(static_cast<ByeReason>(code))));
    else if (!mByeText.empty())
        why = mByeText;
    else
        why = code == 0 ? std::string("connection to the host lost") : std::string(ByeText_cai(static_cast<ByeReason>(code)));
    LOG_INFO("link to the host closed (code {}): {}", code, why);
    Close_cai(ByeReason::Leaving);
    SetState_cai(state == SessionState::InSession ? SessionState::Lost : SessionState::Failed, why);
}

// Connect / handshake timeouts, expired questions.
void NetSession::JoinTimers_cai()
{
    if ((mState == SessionState::Connecting || mState == SessionState::Handshaking) && Clock::now() >= mDeadline) {
        const bool connecting = mState == SessionState::Connecting;
        const std::string why = connecting
            ? fmt::format("no answer from {}:{} in {} s (address, port, firewall / port forwarding?)",
                          mJoinSettings.address, mJoinSettings.port, kConnectTimeout)
            : std::string("the host did not finish the handshake in time");
        LOG_WARN("{}", why);
        Close_cai(ByeReason::Timeout);
        SetState_cai(SessionState::Failed, why);
        return;
    }
    ExpireQuestions_cai();
}

// ======================================================================================
//  Both sides
// ======================================================================================

// The PIC's id.
std::uint8_t NetSession::PicId_cai() const
{
    if (mIsHost)
        return mPic;
    for (const Person& p : mPeople)
        if (p.pic)
            return p.id;
    return mPic;
}

// A person by id.
const Person* NetSession::Find_cai(std::uint8_t id) const
{
    for (const Person& p : mPeople)
        if (p.id == id)
            return &p;
    if (mIsHost)
        for (const auto& [linkId, link] : mLinks)
            if (link.step == Link::Step::In && link.person.id == id)
                return &link.person;
    return nullptr;
}

// A person's name.
std::string NetSession::NameOf_cai(std::uint8_t id) const
{
    if (const Person* p = Find_cai(id))
        return p->name;
    if (id == kHostId && mIsHost)
        return mLocal.name;
    return fmt::format("#{}", id);
}

// A new question for this person.
void NetSession::AddQuestion_cai(Question::Kind kind, std::uint8_t fromId)
{
    auto lmbda_fromSame = [fromId](const Question& q) { return q.fromId == fromId; };
    std::erase_if(mQuestions, lmbda_fromSame);
    Question question{kind, fromId, NameOf_cai(fromId), After_cai(kRequestTimeout)};
    LOG_INFO("question: {} {} (answer within {} s)", question.fromName,
             kind == Question::Kind::Request ? "asks you for control" : "offers you control", kRequestTimeout);
    mQuestions.push_back(question);
    Event_cai(SessionEvent::Kind::Question,
              fmt::format("{} {}", question.fromName, kind == Question::Kind::Request ? "asks for control" : "offers you control"));
}

// Drops questions nobody answered in time.
void NetSession::ExpireQuestions_cai()
{
    const auto now = Clock::now();
    for (auto it = mQuestions.begin(); it != mQuestions.end();) {
        if (now >= it->until) {
            LOG_INFO("question from '{}' expired", it->fromName);
            it = mQuestions.erase(it);
        } else {
            ++it;
        }
    }
}

// Asks the PIC for control.
bool NetSession::RequestControl_cai(std::string& why)
{
    if (!Active_cai()) { why = "not in a session"; return false; }
    if (PicId_cai() == mMyId) { why = "you already have control"; return false; }
    LOG_INFO("control: asking '{}' for control", NameOf_cai(PicId_cai()));
    if (mIsHost)
        HostOnRequest_cai(kHostId);
    else
        Send_cai(mHostLink, MsgControlRequest{});
    return true;
}

// Offers control to someone.
bool NetSession::OfferControl_cai(std::uint8_t toId, std::string& why)
{
    if (!Active_cai()) { why = "not in a session"; return false; }
    if (PicId_cai() != mMyId) { why = "only the pilot in command can offer control"; return false; }
    if (toId == mMyId || !Find_cai(toId)) { why = "no such person"; return false; }
    LOG_INFO("control: offering control to '{}'", NameOf_cai(toId));
    if (mIsHost)
        HostOnOffer_cai(kHostId, toId);
    else
        Send_cai(mHostLink, MsgControlOffer{kNoId, toId});
    return true;
}

// Answers a question.
bool NetSession::AnswerControl_cai(std::uint8_t fromId, bool accept, std::string& why)
{
    auto lmbda_fromAsker = [fromId](const Question& q) { return q.fromId == fromId; };
    const auto found = std::find_if(mQuestions.begin(), mQuestions.end(), lmbda_fromAsker);
    if (found == mQuestions.end()) { why = "no open question from this person"; return false; }
    LOG_INFO("control: answering '{}': {}", found->fromName, accept ? "yes" : "no");
    mQuestions.erase(found);
    if (mIsHost)
        HostOnAnswer_cai(kHostId, fromId, accept);
    else
        Send_cai(mHostLink, MsgControlAnswer{fromId, kNoId, accept});
    return true;
}

// Sends a notice.
bool NetSession::SendNotice_cai(const std::string& text, std::string& why)
{
    if (!Active_cai()) { why = "not in a session"; return false; }
    if (Utils::Trim_cai(text).empty()) { why = "empty message"; return false; }
    if (mIsHost)
        HostOnNotice_cai(kHostId, text);
    else
        Send_cai(mHostLink, MsgNotice{text});
    return true;
}

// The master takes control.
bool NetSession::TakeControl_cai(std::string& why)
{
    if (!mIsHost || mState != SessionState::Hosting) { why = "only the master (the host) can take control"; return false; }
    if (mPic == kHostId) { why = "you already have control"; return false; }
    HostSetPic_cai(kHostId, "the master took control");
    return true;
}

// The master gives someone a seat.
bool NetSession::SetRole_cai(std::uint8_t id, Role role, std::string& why)
{
    if (!mIsHost || mState != SessionState::Hosting) { why = "only the master (the host) assigns seats"; return false; }
    if (id == kHostId) { why = "the master is always crew"; return false; }
    Link* link = HostLinkById_cai(id);
    if (!link) { why = "no such person"; return false; }
    if (link->person.role == role) { why = fmt::format("{} is already {}", link->person.name, RoleName_cai(role)); return false; }
    link->person.role = role;
    LOG_INFO("seat: '{}' is now {}", link->person.name, RoleName_cai(role));
    const std::string text = fmt::format("{} is now {}", link->person.name, RoleName_cai(role));
    HostBroadcast_cai(MsgNotice{text});
    Event_cai(SessionEvent::Kind::Notice, text);
    if (role == Role::Observer && mPic == id)
        HostSetPic_cai(kHostId, fmt::format("{} became an observer, control returns to the host", link->person.name));
    else
        HostRebuildPeople_cai(true);
    return true;
}

// The seat of the next joiners (while hosting; the people already in keep their seats).
bool NetSession::SetJoinersStartAs_cai(Role role, std::string& why)
{
    if (!mIsHost || mState != SessionState::Hosting) { why = "only the master (the host) sets this"; return false; }
    if (mHostSettings.joinersStartAs == role) { why = fmt::format("joiners already start as {}", RoleName_cai(role)); return false; }
    mHostSettings.joinersStartAs = role;
    LOG_INFO("seat: the next joiners start as {}", RoleName_cai(role));
    Event_cai(SessionEvent::Kind::Notice, fmt::format("from now on, joiners start as {}", RoleName_cai(role)));
    return true;
}

// ======================================================================================
//  Shared commands
// ======================================================================================

// A shared command pressed / released here.
bool NetSession::SendCommand_cai(std::uint16_t index, CommandPhase phase, std::string& why)
{
    if (!Active_cai()) { why = "not in a session"; return false; }
    const Person* me = Find_cai(mMyId);
    if (me && me->role == Role::Observer) { why = "observers cannot use the shared controls"; return false; }
    LOG_DEBUG("command {} {} (mine)", CommandText_cai(index), phase == CommandPhase::Begin ? "Begin" : "End");
    if (mIsHost)
        HostOnCommand_cai(kHostId, index, phase);
    else
        Send_cai(mHostLink, MsgCommand{index, phase, kNoId});
    return true;
}

// The host's check of a shared command, then on to everybody but its author.
void NetSession::HostOnCommand_cai(std::uint8_t author, std::uint16_t index, CommandPhase phase)
{
    const Person* who = Find_cai(author);
    if (!who)
        return;
    auto lmbda_reject = [&](std::uint8_t winner, const std::string& text) {
        LOG_INFO("command {} {} by '{}' not shared: {}", CommandText_cai(index), phase == CommandPhase::Begin ? "Begin" : "End",
                 who->name, text);
        if (author == kHostId) {
            mRejections.push_back({index, winner, text});
        } else if (Link* link = HostLinkById_cai(author)) {
            Send_cai(link->link, MsgReject{index, winner, text});
        }
    };
    const auto held = std::find(mHeld.begin(), mHeld.end(), std::make_pair(author, index));
    if (phase == CommandPhase::End) {
        // Only the end of a Begin that was shared (a rejected Begin's End stays here too).
        if (held == mHeld.end())
            return;
        mHeld.erase(held);
    } else {
        if (who->role == Role::Observer) {
            lmbda_reject(kNoId, "observers cannot use the shared controls");
            return;
        }
        const auto last = mLastBegin.find(index);
        const auto now = Clock::now();
        if (last != mLastBegin.end() && last->second.author != author &&
            std::chrono::duration<double>(now - last->second.at).count() < mRules.conflictMs / 1000.0 &&
            Rank_cai(last->second.author) < Rank_cai(author)) {
            lmbda_reject(last->second.author,
                         fmt::format("{} changed it less than {:.1f} s before", NameOf_cai(last->second.author), mRules.conflictMs / 1000.0));
            return;
        }
        mLastBegin[index] = {author, now};
        if (held == mHeld.end())
            mHeld.push_back({author, index});
    }
    LOG_DEBUG("command {} {} by '{}' shared", CommandText_cai(index), phase == CommandPhase::Begin ? "Begin" : "End", who->name);
    const MsgCommand out{index, phase, author};
    for (const auto& [linkId, link] : mLinks)
        if (link.step == Link::Step::In && link.person.id != author)
            Send_cai(link.link, out);
    if (author != kHostId)
        mCommandsIn.push_back({index, phase, author});
}

// Ends every command `author` still holds (they left): nobody keeps a starter held forever.
void NetSession::HostReleaseHeld_cai(std::uint8_t author)
{
    for (auto it = mHeld.begin(); it != mHeld.end();) {
        if (it->first != author) {
            ++it;
            continue;
        }
        const std::uint16_t index = it->second;
        it = mHeld.erase(it);
        LOG_INFO("command {} was still held by '{}': ended for everybody", CommandText_cai(index), NameOf_cai(author));
        const MsgCommand out{index, CommandPhase::End, author};
        for (const auto& [linkId, link] : mLinks)
            if (link.step == Link::Step::In && link.person.id != author)
                Send_cai(link.link, out);
        mCommandsIn.push_back({index, CommandPhase::End, author});
    }
}

// Commands from the others.
std::vector<RemoteCommand> NetSession::TakeCommands_cai()
{
    std::vector<RemoteCommand> out;
    out.swap(mCommandsIn);
    return out;
}

// This simulator's commands the host did not share.
std::vector<Rejection> NetSession::TakeRejections_cai()
{
    std::vector<Rejection> out;
    out.swap(mRejections);
    return out;
}

// "#137 ANNUNCIATOR TEST (sim/...)" for the log.
std::string NetSession::CommandText_cai(std::uint16_t index) const
{
    const std::string name = mCommandNamer ? mCommandNamer(index) : std::string();
    return name.empty() ? fmt::format("#{}", index) : fmt::format("#{} {}", index, name);
}

// 0 = host, 1 = PIC, then 2 + the place in the priority list.
int NetSession::Rank_cai(std::uint8_t id) const
{
    if (id == kHostId)
        return 0;
    if (id == PicId_cai())
        return 1;
    const auto at = std::find(mPriority.begin(), mPriority.end(), id);
    return at == mPriority.end() ? 100 + id : 2 + static_cast<int>(at - mPriority.begin());
}

// 1 = the host, then 2, 3, 4 by the priority list.
int NetSession::PriorityOf_cai(std::uint8_t id) const
{
    if (id == kHostId)
        return 1;
    const auto at = std::find(mPriority.begin(), mPriority.end(), id);
    return at == mPriority.end() ? 0 : 2 + static_cast<int>(at - mPriority.begin());
}

// The master moves someone in the priority list.
bool NetSession::MovePriority_cai(std::uint8_t id, int delta, std::string& why)
{
    if (!mIsHost || mState != SessionState::Hosting) { why = "only the master (the host) sets the priority"; return false; }
    const auto at = std::find(mPriority.begin(), mPriority.end(), id);
    if (at == mPriority.end()) { why = "no such person (the host is always first)"; return false; }
    const auto from = at - mPriority.begin();
    const auto to = from + delta;
    if (to < 0 || to >= static_cast<std::ptrdiff_t>(mPriority.size())) { why = "already at the end of the list"; return false; }
    std::swap(mPriority[static_cast<std::size_t>(from)], mPriority[static_cast<std::size_t>(to)]);
    LOG_INFO("priority: '{}' is now {}", NameOf_cai(id), PriorityOf_cai(id));
    HostSendPriority_cai();
    Event_cai(SessionEvent::Kind::People, {});
    return true;
}

// Sends the priority list to everybody.
void NetSession::HostSendPriority_cai()
{
    HostBroadcast_cai(MsgPriority{mPriority});
}

// Values changed here.
bool NetSession::SendValues_cai(const std::vector<DataValue>& values, std::string& why)
{
    if (!Active_cai()) { why = "not in a session"; return false; }
    const Person* me = Find_cai(mMyId);
    if (me && me->role == Role::Observer) { why = "observers cannot use the shared controls"; return false; }
    if (values.empty())
        return true;
    LOG_DEBUG("values: {} changed here", values.size());
    if (mIsHost) {
        HostOnValues_cai(kHostId, values);
    } else {
        for (std::size_t at = 0; at < values.size(); at += kMaxValuesPerMessage) {
            const auto end = values.begin() + static_cast<std::ptrdiff_t>(std::min(values.size(), at + kMaxValuesPerMessage));
            Send_cai(mHostLink, MsgValues{kNoId, std::vector<DataValue>(values.begin() + static_cast<std::ptrdiff_t>(at), end)});
        }
    }
    return true;
}

// The host's check of shared values: the 1 s rule per dataref, observers corrected.
void NetSession::HostOnValues_cai(std::uint8_t author, const std::vector<DataValue>& values)
{
    const Person* who = Find_cai(author);
    if (!who)
        return;
    std::vector<DataValue> accepted, corrected;
    std::uint8_t winner = kNoId;
    std::string why;
    const auto now = Clock::now();
    for (const DataValue& v : values) {
        bool reject = false;
        if (who->role == Role::Observer) {
            reject = true;
            why = "observers cannot use the shared controls";
        } else if (const auto last = mLastValue.find(v.index); last != mLastValue.end() &&
                   last->second.author != author &&
                   std::chrono::duration<double>(now - last->second.at).count() < mRules.conflictMs / 1000.0 &&
                   Rank_cai(last->second.author) < Rank_cai(author)) {
            reject = true;
            winner = last->second.author;
            why = fmt::format("{} changed it less than {:.1f} s before", NameOf_cai(winner), mRules.conflictMs / 1000.0);
        }
        if (reject) {
            if (const auto known = mSessionValues.find(v.index); known != mSessionValues.end())
                corrected.push_back({v.index, known->second});
            continue;
        }
        mLastValue[v.index] = {author, now};
        mSessionValues[v.index] = v.value;
        accepted.push_back(v);
    }
    if (!accepted.empty()) {
        LOG_DEBUG("values by '{}': {} shared", who->name, accepted.size());
        const MsgValues out{author, accepted};
        for (const auto& [linkId, link] : mLinks)
            if (link.step == Link::Step::In && link.person.id != author)
                Send_cai(link.link, out);
        if (author != kHostId)
            mValuesIn.push_back({RemoteValues::Kind::Change, author, {}, accepted});
    }
    if (!corrected.empty() || (accepted.size() < values.size())) {
        LOG_INFO("values by '{}': {} not shared ({}), {} snapped back", who->name, values.size() - accepted.size(), why,
                 corrected.size());
        if (author != kHostId && !corrected.empty())
            if (Link* link = HostLinkById_cai(author))
                Send_cai(link->link, MsgCorrection{winner, why, corrected});
    }
}

// Values for this simulator.
std::vector<RemoteValues> NetSession::TakeValues_cai()
{
    std::vector<RemoteValues> out;
    out.swap(mValuesIn);
    return out;
}

// People who need the snapshot.
std::vector<std::uint8_t> NetSession::TakeSnapshotRequests_cai()
{
    std::vector<std::uint8_t> out;
    out.swap(mSnapshotRequests);
    return out;
}

// The rules I use when I host (from my settings). While hosting they apply at once and go to everybody.
void NetSession::SetOwnRules_cai(const SessionRules& rules)
{
    SessionRules clamped = rules;
    clamped.Clamp_cai();
    const bool changed = !(clamped == mOwnRules);
    mOwnRules = clamped;
    if (!mIsHost || mState != SessionState::Hosting) {
        if (!Active_cai())
            mRules = mOwnRules;
        return;
    }
    if (!changed && clamped == mRules)
        return;
    mRules = mOwnRules;
    LOG_INFO("session rules changed: {}", RulesText_cai(mRules));
    HostBroadcast_cai(MsgRules{mRules});
    Event_cai(SessionEvent::Kind::Notice, "Session settings changed (sent to everybody)");
}

// "updates 10/s, conflicts 1.0 s, move: 5 m / 3° / 250 m (ask), weather every 60 s, time (30 s)".
std::string NetSession::RulesText_cai(const SessionRules& r)
{
    return fmt::format("{}, updates {}/s, conflicts {:.1f} s, move: {} m / {}° / {} m{}, weather {}, time {}",
                       r.flight == FlightMode::Follow
                           ? fmt::format("follow the PIC ({} ms{})", r.smoothingMs, r.followStrict ? ", strict" : "")
                           : std::string("inputs only"),
                       r.rate, r.conflictMs / 1000.0, r.groundM, r.headingDeg, r.airM, r.askOnJoin ? " (ask on join)" : "",
                       r.weather ? fmt::format("every {} s", r.weatherS) : std::string("off"),
                       r.time ? fmt::format("on ({} s)", r.timeToleranceS) : std::string("off")) +
           (r.handoverBlendMs > 0 ? fmt::format(", handover {:.0f} s", r.handoverBlendMs / 1000.0) : std::string(", handover at once")) +
           (r.shareLoad ? ", fuel and payload" : "") + (r.copilotAxes ? ", copilot axes ON" : "");
}

// A joiner asks for the snapshot again.
bool NetSession::RequestSnapshot_cai(std::string& why)
{
    if (mIsHost && mState == SessionState::Hosting) {
        // The host's own aircraft was reloaded: write back the session's values it keeps.
        std::vector<DataValue> values;
        for (const auto& [index, value] : mSessionValues)
            values.push_back({index, value});
        if (values.empty()) { why = "no session values yet (nobody joined)"; return false; }
        LOG_INFO("the session's {} values back into my reloaded aircraft", values.size());
        mValuesIn.push_back({RemoteValues::Kind::Snapshot, kHostId, {}, values});
        return true;
    }
    if (mState != SessionState::InSession) { why = "not in a session"; return false; }
    LOG_INFO("asking the host for the snapshot again");
    Send_cai(mHostLink, MsgSnapshotRequest{});
    return true;
}

// My position: kept, and sent to the host (a joiner) or to everybody (the host).
void NetSession::SendPosition_cai(const PositionInfo& position)
{
    if (!Active_cai())
        return;
    mPositions[mMyId] = {position, Clock::now()};
    if (mIsHost) {
        const MsgPosition out{kHostId, position};
        for (const auto& [linkId, link] : mLinks)
            if (link.step == Link::Step::In)
                Send_cai(link.link, out, true);
    } else {
        Send_cai(mHostLink, MsgPosition{kNoId, position}, true);
    }
}

// The host's weather and time to everybody.
void NetSession::SendWeather_cai(const std::vector<DataValue>& values)
{
    if (!mIsHost || mState != SessionState::Hosting)
        return;
    const MsgWeather out{values};
    for (const auto& [linkId, link] : mLinks)
        if (link.step == Link::Step::In)
            Send_cai(link.link, out, true);
}

// The PIC: my flight inputs to everybody.
void NetSession::SendFlightInputs_cai(const FlightInputs& inputs)
{
    if (!Active_cai() || PicId_cai() != mMyId)
        return;
    const MsgFlightInputs out{mIsHost ? kHostId : kNoId, ++mInputsSequence, inputs};
    if (mIsHost) {
        for (const auto& [linkId, link] : mLinks)
            if (link.step == Link::Step::In)
                Send_cai(link.link, out, true, false);
    } else {
        Send_cai(mHostLink, out, true, false);
    }
}

// The PIC: my aircraft's state to everybody (Follow).
void NetSession::SendFlightState_cai(const AircraftState& state, std::uint32_t timeMs)
{
    if (!Active_cai() || PicId_cai() != mMyId)
        return;
    const MsgFlightState out{mIsHost ? kHostId : kNoId, ++mStateSequence, timeMs, state};
    if (mIsHost) {
        for (const auto& [linkId, link] : mLinks)
            if (link.step == Link::Step::In)
                Send_cai(link.link, out, true, false);
    } else {
        Send_cai(mHostLink, out, true, false);
    }
}

// The PIC's newest aircraft state.
std::optional<NetSession::StateIn> NetSession::TakeFlightState_cai()
{
    std::optional<StateIn> out;
    out.swap(mStateIn);
    return out;
}

// The PIC: my fuel and payload to everybody.
void NetSession::SendLoad_cai(const LoadInfo& load)
{
    if (!Active_cai() || PicId_cai() != mMyId || !mRules.shareLoad)
        return;
    if (mIsHost) {
        mPicLoad = load;
        for (const auto& [linkId, link] : mLinks)
            if (link.step == Link::Step::In)
                Send_cai(link.link, MsgLoad{kHostId, load}, true);
    } else {
        Send_cai(mHostLink, MsgLoad{kNoId, load}, true);
    }
}

// My aircraft's seats; hosting, everybody gets them (and their picks are cleared).
void NetSession::SetOwnSeats_cai(const std::vector<SeatInfo>& seats)
{
    if (seats == mOwnSeats)
        return;
    mOwnSeats = seats;
    if (mOwnSeats.size() > static_cast<std::size_t>(kMaxSeats))
        mOwnSeats.resize(kMaxSeats);
    if (!Active_cai() || mIsHost) {
        mOwnSeat = kNoSeat;
        mSeatsChanged = true;
    }
    if (Active_cai() && mIsHost) {
        for (auto& [linkId, link] : mLinks)
            link.person.seat = kNoSeat;
        HostBroadcast_cai(MsgSeats{mOwnSeats});
        HostRebuildPeople_cai(true);
        LOG_INFO("seats sent to everybody: {}", mOwnSeats.size());
    }
}

// A joiner: the host's seats; else mine.
const std::vector<SeatInfo>& NetSession::SessionSeats_cai() const
{
    return Active_cai() && !mIsHost ? mHostSeats : mOwnSeats;
}

// My pick, shown to everybody.
void NetSession::PickSeat_cai(std::uint8_t seat)
{
    if (seat >= SessionSeats_cai().size())
        seat = kNoSeat;
    mOwnSeat = seat;
    if (!Active_cai())
        return;
    if (mIsHost)
        HostRebuildPeople_cai(true);
    else
        Send_cai(mHostLink, MsgSeatChoice{kNoId, seat});
}

// My pick.
std::uint8_t NetSession::MySeat_cai() const
{
    return mOwnSeat;
}

// True once after the session's seats changed.
bool NetSession::TakeSeatsChanged_cai()
{
    const bool changed = mSeatsChanged;
    mSeatsChanged = false;
    return changed;
}

// The PIC's fuel and payload when they arrived since the last call.
std::optional<LoadInfo> NetSession::TakeLoad_cai()
{
    std::optional<LoadInfo> out;
    out.swap(mLoadIn);
    return out;
}

// About how long the PIC's packets take to reach me (half of the PIC's and my ping to the host), ms.
double NetSession::PicDelayMs_cai() const
{
    double ms = 0.0;
    for (const std::uint8_t id : {PicId_cai(), mMyId})
        if (const Person* who = Find_cai(id); who && id != kHostId)
            ms += who->pingMs / 2.0;
    return ms;
}

// The newest flight inputs of the PIC.
std::optional<FlightInputs> NetSession::TakeFlightInputs_cai()
{
    std::optional<FlightInputs> out;
    out.swap(mInputsIn);
    return out;
}

// True when I may send copilot inputs now.
bool NetSession::MayCopilot_cai() const
{
    if (!Active_cai() || !mRules.copilotAxes || PicId_cai() == mMyId)
        return false;
    const Person* me = Find_cai(mMyId);
    return me && me->role == Role::Crew;
}

// Host: true when `id` may have the copilot seat now - Crew, not the PIC, the rule on, and nobody
// ranked higher sent in the last second.
bool NetSession::HostCopilotSeat_cai(std::uint8_t id)
{
    if (!mRules.copilotAxes || id == PicId_cai())
        return false;
    const Person* who = Find_cai(id);
    if (!who || who->role != Role::Crew)
        return false;
    const auto now = Clock::now();
    const bool seatFree = mCopilotId == kNoId || now - mCopilotAt > std::chrono::seconds(1) || !Find_cai(mCopilotId);
    if (id != mCopilotId && !seatFree && Rank_cai(id) >= Rank_cai(mCopilotId))
        return false;                                     // somebody ranked higher has the copilot seat
    if (id != mCopilotId)
        LOG_INFO("copilot axes: '{}' has the copilot seat", NameOf_cai(id));
    mCopilotId = id;
    mCopilotAt = now;
    return true;
}

// My stick / yoke / pedals as the copilot side, to everybody.
void NetSession::SendCopilotInputs_cai(const CopilotInputs& inputs)
{
    if (!MayCopilot_cai())
        return;
    if (mIsHost) {
        if (!HostCopilotSeat_cai(mMyId))
            return;
        const MsgCopilotInputs out{kHostId, inputs};
        for (const auto& [linkId, link] : mLinks)
            if (link.step == Link::Step::In)
                Send_cai(link.link, out, true, false);
    } else {
        Send_cai(mHostLink, MsgCopilotInputs{kNoId, inputs}, true, false);
    }
}

// The copilot's newest inputs.
std::optional<NetSession::CopilotIn> NetSession::TakeCopilotInputs_cai()
{
    std::optional<CopilotIn> out;
    out.swap(mCopilotIn);
    return out;
}

// The PIC: my pause state to everybody.
void NetSession::SendPause_cai(bool paused)
{
    if (!Active_cai() || PicId_cai() != mMyId)
        return;
    LOG_INFO("pause: I {} the flight (PIC)", paused ? "paused" : "resumed");
    if (mIsHost) {
        mPicPaused = paused;
        HostBroadcast_cai(MsgPause{kHostId, paused});
    } else {
        Send_cai(mHostLink, MsgPause{kNoId, paused});
    }
}

// The PIC's pause state when it arrived.
std::optional<bool> NetSession::TakePause_cai()
{
    std::optional<bool> out;
    out.swap(mPauseIn);
    return out;
}

// The newest weather from the host.
std::optional<std::vector<DataValue>> NetSession::TakeWeather_cai()
{
    std::optional<std::vector<DataValue>> out;
    out.swap(mWeatherIn);
    return out;
}

// Sends every shared value to a joiner, in parts.
void NetSession::SendSnapshot_cai(std::uint8_t id, const std::vector<DataValue>& values)
{
    for (const DataValue& v : values)
        mSessionValues[v.index] = v.value;              // read from X-Plane now: the session's values
    Link* link = HostLinkById_cai(id);
    if (!link)
        return;
    const std::size_t parts = std::max<std::size_t>(1, (values.size() + kMaxValuesPerMessage - 1) / kMaxValuesPerMessage);
    for (std::size_t part = 0; part < parts; ++part) {
        const auto begin = values.begin() + static_cast<std::ptrdiff_t>(std::min(values.size(), part * kMaxValuesPerMessage));
        const auto end = values.begin() + static_cast<std::ptrdiff_t>(std::min(values.size(), (part + 1) * kMaxValuesPerMessage));
        Send_cai(link->link, MsgSnapshot{static_cast<std::uint8_t>(part), static_cast<std::uint8_t>(parts),
                                         std::vector<DataValue>(begin, end)});
    }
    LOG_INFO("snapshot to '{}': {} values", link->person.name, values.size());
}

// The master removes someone.
bool NetSession::Kick_cai(std::uint8_t id, std::string& why)
{
    if (!mIsHost || mState != SessionState::Hosting) { why = "only the master (the host) can remove people"; return false; }
    Link* link = HostLinkById_cai(id);
    if (!link) { why = "no such person"; return false; }
    const LinkId linkId = link->link;
    Send_cai(linkId, MsgBye{ByeReason::Kicked, "removed by the master"});
    HostRemove_cai(*link, "removed by the master");
    mNet.Disconnect_cai(linkId, static_cast<std::uint32_t>(ByeReason::Kicked));
    return true;
}

} // namespace sc::net
