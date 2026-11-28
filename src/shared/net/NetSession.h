// shared_cockpit - one shared-cockpit session, seen from this simulator (main thread)
//
// NetSession turns the bytes of NetThread into a session: the handshake, the
// people and their seats, who is the pilot in command (PIC), notices, kicks.
// It is used the same way by the plugin (from a flight loop) and by sc_net_test:
//     session.SetLocal_cai(info);        // who am I, which aircraft, which analysis
//     session.Host_cai(settings) / session.Join_cai(settings);
//     every frame: session.Poll_cai(); then TakeEvents_cai() for the screens
//
// v1 rules (see README.md):
//   * the host is the master for the whole session; the host leaving ends it
//   * joiners start as Crew, or as Observer when the host chose "I assign seats"
//   * the host's analysis (profile) is used by everybody, in memory only
//   * a different .acf name is refused, a different .acf stamp only warned about
//   * the PIC changes by request + answer, by offer + answer, or by the master
//     taking control; when the PIC leaves, control returns to the host
// Every step is logged: Info for states, handshake, seats, control and
// disconnects; Debug for every message sent and received.

#pragma once

#include <chrono>
#include <functional>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "shared/analysis/Analysis.h"
#include "shared/net/Messages.h"
#include "shared/net/NetThread.h"

namespace sc::net {

// Who this simulator is.
struct LocalInfo {
    std::string              name;            // shown to the others
    std::string              pluginVersion;
    std::string              acfName;         // "Cessna_172SP_G1000.acf"
    std::string              acfStamp;
    std::string              profileText;     // the analysis file's text (the host sends it)
    std::string              profileHash;     // analysis::ProfileHash_cai
    std::vector<std::string> allKeys;         // analysis::AllKeys_cai (a joiner checks the host's profile with it)
};

struct HostSettings {
    std::uint16_t port = kDefaultPort;
    std::string   password;                   // empty = no password
    int           maxPeople = kMaxPeople;     // 2..4, the host included
    Role          joinersStartAs = Role::Crew;   // Observer = "I assign seats"
};

struct JoinSettings {
    std::string   address;                    // a name or an IP (LAN, forwarded port, ZeroTier, Tailscale)
    std::uint16_t port = kDefaultPort;
    std::string   password;
};

enum class SessionState {
    Idle,           // nothing running
    Hosting,        // the host: the session is open
    Connecting,     // a joiner: waiting for the host to answer
    Handshaking,    // a joiner: connected, waiting for Welcome and the profile
    InSession,      // a joiner: in the session
    Failed,         // could not start / join (FailText_cai says why)
    Lost,           // the session ended (host left, kicked, connection lost)
};

// A question this person has to answer (shown with Yes / No).
struct Question {
    enum class Kind { Request, Offer };       // Request: someone wants control from me / Offer: I am offered control
    Kind         kind = Kind::Request;
    std::uint8_t fromId = kNoId;
    std::string  fromName;
    std::chrono::steady_clock::time_point until;
};

// Something the screens (or the test tool) should show.
struct SessionEvent {
    enum class Kind {
        State,          // the state changed (text = why)
        People,         // the people list changed
        Question,       // a new question (Questions_cai)
        ControlChanged, // the PIC changed (text = why)
        Declined,       // my request / offer was declined or not answered (text)
        Notice,         // a message from someone (text = "name: message")
        Warning,        // something to know (text), e.g. a different aircraft version
    };
    Kind        kind = Kind::State;
    std::string text;
};

// A shared command to run here, pressed by `author` on another simulator.
struct RemoteCommand {
    std::uint16_t index = 0;            // in analysis::SharedCommands_cai of the session's analysis
    CommandPhase  phase = CommandPhase::Begin;
    std::uint8_t  author = kNoId;
};

// The host did not share this simulator's command (someone changed it first, or an observer).
struct Rejection {
    std::uint16_t index = 0;
    std::uint8_t  winner = kNoId;
    std::string   text;
};

// Shared dataref values from the host, to write here.
struct RemoteValues {
    enum class Kind {
        Change,         // someone changed them
        Correction,     // the host did not share my changes: snap back to these
        Snapshot,       // the session's values, sent to me when I joined
    };
    Kind                   kind = Kind::Change;
    std::uint8_t           author = kNoId;    // who changed them (Correction: who won)
    std::string            text;              // Correction: why
    std::vector<DataValue> values;
};

const char* StateName_cai(SessionState state);

class NetSession {
public:
    NetSession() = default;
    ~NetSession();

    NetSession(const NetSession&)            = delete;
    NetSession& operator=(const NetSession&) = delete;

    // Who this simulator is (used by the next Host_cai / Join_cai).
    void SetLocal_cai(LocalInfo info);
    const LocalInfo& Local_cai() const { return mLocal; }

    // Opens a session as the host. False (FailText_cai) when the settings are wrong.
    bool Host_cai(const HostSettings& settings);
    // Joins a host. False when the settings are wrong; the answer comes later (state).
    bool Join_cai(const JoinSettings& settings);
    // Leaves (a joiner) or ends (the host) the session; back to Idle.
    void Leave_cai();

    // Handles the network events and the timers. Call often (every frame / 10 ms).
    void Poll_cai();

    SessionState State_cai() const { return mState; }
    bool IsHost_cai() const { return mIsHost; }
    // True when hosting or in a session.
    bool Active_cai() const { return mState == SessionState::Hosting || mState == SessionState::InSession; }
    const std::string& FailText_cai() const { return mFailText; }
    const std::vector<Person>& People_cai() const { return mPeople; }
    std::uint8_t MyId_cai() const { return mMyId; }
    std::uint8_t PicId_cai() const;
    // A person by id (nullptr when not in the session).
    const Person* Find_cai(std::uint8_t id) const;
    // A person's name, or "#<id>".
    std::string NameOf_cai(std::uint8_t id) const;
    // A joiner: the host's analysis, used for this session (empty until it arrived or when
    // both had the same one: then the own analysis is used).
    const std::optional<analysis::AnalysisData>& HostProfile_cai() const { return mHostProfile; }
    // Questions this person has to answer.
    const std::vector<Question>& Questions_cai() const { return mQuestions; }
    // The host's settings in use (the host only).
    const HostSettings& HostSettings_cai() const { return mHostSettings; }
    // The join settings in use (a joiner only).
    const JoinSettings& JoinSettings_cai() const { return mJoinSettings; }

    // --- Control (everybody) ---------------------------------------------------------
    // Asks the PIC for control.
    bool RequestControl_cai(std::string& why);
    // The PIC offers control to `toId`.
    bool OfferControl_cai(std::uint8_t toId, std::string& why);
    // Answers a question from `fromId`.
    bool AnswerControl_cai(std::uint8_t fromId, bool accept, std::string& why);
    // Sends a message to everybody.
    bool SendNotice_cai(const std::string& text, std::string& why);

    // --- The master (the host in v1) -----------------------------------------------------
    // Takes control at once (no question).
    bool TakeControl_cai(std::string& why);
    // Gives `id` a seat.
    bool SetRole_cai(std::uint8_t id, Role role, std::string& why);
    // Removes `id` from the session.
    bool Kick_cai(std::uint8_t id, std::string& why);
    // The seat of the next joiners, changed while hosting (people already in keep theirs).
    bool SetJoinersStartAs_cai(Role role, std::string& why);

    // --- Shared controls (step 2) -------------------------------------------------------
    // A shared command was pressed / released here (it already runs here). The host checks
    // it and sends it to the others. False with `why` when it is not sent (an observer).
    bool SendCommand_cai(std::uint16_t index, CommandPhase phase, std::string& why);
    // Commands from the others, to run here.
    std::vector<RemoteCommand> TakeCommands_cai();
    // This simulator's commands the host did not share.
    std::vector<Rejection> TakeRejections_cai();
    // Shared dataref values changed here (already changed here). The host checks them and sends
    // them on; changes it does not share come back as a Correction. False with `why`: an observer.
    bool SendValues_cai(const std::vector<DataValue>& values, std::string& why);
    // Values from the host to write here (changes, corrections, the snapshot).
    std::vector<RemoteValues> TakeValues_cai();
    // Host: people who joined and need the snapshot (their ids).
    std::vector<std::uint8_t> TakeSnapshotRequests_cai();
    // Host: sends every shared value to `id` (in parts) and keeps them as the session's values.
    void SendSnapshot_cai(std::uint8_t id, const std::vector<DataValue>& values);

    // The rules I use when I host (my [session] settings); while hosting: applied at once, sent to all.
    void SetOwnRules_cai(const SessionRules& rules);
    // The rules in force: the host's (a joiner gets them on admit), mine when idle or hosting.
    const SessionRules& Rules_cai() const { return mRules; }
    // True when a joiner has the host's rules (false: mine, until they arrive).
    bool RulesFromHost_cai() const { return mRulesFromHost; }
    // "updates 10/s, conflicts 1.0 s, ..." for the log.
    static std::string RulesText_cai(const SessionRules& rules);

    // My aircraft was reloaded: a joiner asks the host for the snapshot again; the host writes
    // back the session's values it keeps (as a Snapshot from TakeValues_cai).
    bool RequestSnapshot_cai(std::string& why);

    // Where this simulator's aircraft is (every kPositionInterval): the host sends it on to everybody.
    void SendPosition_cai(const PositionInfo& position);
    // The last known position of everybody in the session (mine included), by id.
    struct KnownPosition { PositionInfo position; std::chrono::steady_clock::time_point at; };
    const std::map<std::uint8_t, KnownPosition>& Positions_cai() const { return mPositions; }
    // Host: the weather and time to everybody.
    void SendWeather_cai(const std::vector<DataValue>& values);
    // Joiner: the newest weather from the host (empty when none came since the last call).
    std::optional<std::vector<DataValue>> TakeWeather_cai();

    // The PIC: my flight inputs to everybody (unreliable stream; ignored when I am not the PIC).
    void SendFlightInputs_cai(const FlightInputs& inputs);
    // The newest flight inputs of the PIC (empty when none came since the last call).
    std::optional<FlightInputs> TakeFlightInputs_cai();
    // Follow: the PIC sends its aircraft's state (unreliable stream; ignored when I am not the PIC).
    // timeMs: my own clock when the state was read.
    void SendFlightState_cai(const AircraftState& state, std::uint32_t timeMs);
    // The PIC's newest aircraft state, its number, the PIC's clock and when it arrived here
    // (empty when none came since the last call).
    struct StateIn {
        AircraftState state;
        std::uint32_t sequence = 0, timeMs = 0;
        std::chrono::steady_clock::time_point at;
    };
    std::optional<StateIn> TakeFlightState_cai();
    // The PIC: my fuel and payload to everybody (session rule shareLoad; the host keeps it for new joiners).
    void SendLoad_cai(const LoadInfo& load);
    // The PIC's fuel and payload when they arrived since the last call.
    std::optional<LoadInfo> TakeLoad_cai();
    // Seats (0.9.6, protocol 10): my aircraft's vrconfig hotspots. Hosting: sent to everybody
    // when they change (everybody's pick is cleared then).
    void SetOwnSeats_cai(const std::vector<SeatInfo>& seats);
    // The seats to pick from: a joiner the host's, else my own.
    const std::vector<SeatInfo>& SessionSeats_cai() const;
    // I picked a seat (index in SessionSeats_cai, kNoSeat: none): shown to everybody.
    void PickSeat_cai(std::uint8_t seat);
    // My pick (kNoSeat: none).
    std::uint8_t MySeat_cai() const;
    // True once after the session's seats changed.
    bool TakeSeatsChanged_cai();
    // Crew levers (0.9.12, protocol 11). The host gives a Crew member levers (LeverBits) to work:
    // their hands move them and everybody, the PIC too, follows. One owner per lever; the PIC and
    // Observers own none (the PIC works every lever nobody else has). The host only.
    bool SetLevers_cai(std::uint8_t id, std::uint16_t levers, std::string& why);
    // The levers I work now (0: none).
    std::uint16_t MyLevers_cai() const;
    // The levers somebody else works (all of them OR'ed; the PIC's own levers are never here).
    std::uint16_t OthersLevers_cai() const;
    // A lever owner: my levers' positions to everybody.
    void SendLevers_cai(const FlightInputs& inputs);
    struct LeversIn { std::uint16_t levers = 0; FlightInputs inputs; std::chrono::steady_clock::time_point at; };
    // The newest lever positions of each owner (id -> theirs).
    const std::map<std::uint8_t, LeversIn>& LeversIn_cai() const { return mLeversIn; }
    // About how long the PIC's packets take to reach me, ms (from the pings).
    double PicDelayMs_cai() const;
    // Copilot axes (session rule): my stick / yoke / pedals as X-Plane's copilot side, to everybody.
    // Sent only when the rule is on and I am Crew, not the PIC; the host keeps one copilot (the
    // highest rank among those sending) and drops the others.
    void SendCopilotInputs_cai(const CopilotInputs& inputs);
    // The copilot's newest inputs and who (empty when none came since the last call).
    struct CopilotIn { std::uint8_t id = kNoId; CopilotInputs inputs; };
    std::optional<CopilotIn> TakeCopilotInputs_cai();
    // True when I may send copilot inputs now (rule on, Crew, not the PIC).
    bool MayCopilot_cai() const;
    // The PIC: my pause state to everybody (on a change; the host gives it to new joiners).
    void SendPause_cai(bool paused);
    // The PIC's pause state when it arrived since the last call.
    std::optional<bool> TakePause_cai();

    // The rank of a person for conflicts: 0 = the host, 1 = the PIC, then 2 + the place in
    // the priority list. Lower wins.
    int Rank_cai(std::uint8_t id) const;
    // The priority shown on the Connect page: 1 = the host, then 2, 3, 4 (0 = unknown).
    int PriorityOf_cai(std::uint8_t id) const;
    // The master moves `id` up (delta -1) or down (+1) in the priority list.
    bool MovePriority_cai(std::uint8_t id, int delta, std::string& why);

    // Gives the command list's names to the log (the sync engine / the test tool set it).
    void SetCommandNamer_cai(std::function<std::string(std::uint16_t)> namer) { mCommandNamer = std::move(namer); }

    // Takes the events since the last call.
    std::vector<SessionEvent> TakeEvents_cai();

private:
    using Clock = std::chrono::steady_clock;

    // A joiner, as the host sees it.
    struct Link {
        enum class Step { Hello, Proof, ProfileAck, In, Leaving };
        LinkId       link = kNoLink;
        Step         step = Step::Hello;
        std::string  address;
        Nonce        nonce{};
        MsgHello     hello;
        Person       person;
        Clock::time_point deadline;
    };
    // A request / offer waiting for an answer (the host keeps them).
    struct Pending {
        Question::Kind kind = Question::Kind::Request;
        std::uint8_t   fromId = kNoId;      // who asked / offered
        std::uint8_t   toId = kNoId;        // who must answer
        Clock::time_point until;
    };

    // Before Host / Join: leaves a running session, or quietly clears an ended one.
    void ResetForStart_cai();
    // Sets the state and reports it.
    void SetState_cai(SessionState state, const std::string& why);
    // Adds an event for the screens.
    void Event_cai(SessionEvent::Kind kind, std::string text);
    // Sends a message to a link (logged).
    // `quiet`: not logged (the People heartbeat when nothing changed).
    void Send_cai(LinkId link, const Message& message, bool quiet = false, bool reliable = true);
    // "#137 ANNUNCIATOR TEST (sim/annunciator/test_all_annunciators)" (or "#137" without a namer).
    std::string CommandText_cai(std::uint16_t index) const;
    // Stops the network thread and resets the session data (the state is kept).
    void Close_cai(ByeReason reason);

    // Host side.
    void HostOnConnected_cai(LinkId link, const std::string& address);
    void HostOnReceived_cai(Link& link, const Message& message);
    void HostOnDisconnected_cai(LinkId link, std::uint32_t code);
    void HostRefuse_cai(Link& link, RefuseReason reason, const std::string& text);
    void HostRemove_cai(Link& link, const std::string& why);
    void HostWelcome_cai(Link& link);
    void HostAdmit_cai(Link& link, const MsgProfileAck& ack);
    void HostOnRequest_cai(std::uint8_t fromId);
    void HostOnOffer_cai(std::uint8_t fromId, std::uint8_t toId);
    void HostOnAnswer_cai(std::uint8_t answerId, std::uint8_t askerId, bool accept);
    void HostOnNotice_cai(std::uint8_t fromId, const std::string& text);
    void HostSetPic_cai(std::uint8_t newPic, const std::string& reason);
    void HostDeliverQuestion_cai(const Pending& pending);
    void HostDeclined_cai(std::uint8_t askerId, const std::string& text);
    void HostBroadcast_cai(const Message& message);
    void HostRebuildPeople_cai(bool send);
    void HostTimers_cai();
    Link* HostLinkById_cai(std::uint8_t id);
    std::uint8_t HostFreeId_cai() const;

    // Joiner side.
    void JoinOnReceived_cai(const Message& message);
    void JoinOnDisconnected_cai(std::uint32_t code);
    void JoinCheckProfile_cai(const MsgProfile& profile);
    void JoinTimers_cai();

    // Host: a shared command from `author` (accept and send on, or reject).
    void HostOnCommand_cai(std::uint8_t author, std::uint16_t index, CommandPhase phase);
    // Host: ends every command `author` still holds (they left).
    void HostReleaseHeld_cai(std::uint8_t author);
    // Host: shared values from `author` (accept and send on, correct the rejected ones).
    void HostOnValues_cai(std::uint8_t author, const std::vector<DataValue>& values);
    // Host: sends the priority list to everybody.
    void HostSendPriority_cai();

    // Both sides.
    void AddQuestion_cai(Question::Kind kind, std::uint8_t fromId);
    void ExpireQuestions_cai();

    NetThread         mNet;
    LocalInfo         mLocal;
    HostSettings      mHostSettings;
    JoinSettings      mJoinSettings;
    SessionState      mState = SessionState::Idle;
    bool              mIsHost = false;
    std::string       mFailText;
    std::vector<Person> mPeople;
    std::uint8_t      mMyId = kNoId;
    std::uint8_t      mPic = kHostId;               // the host's view (joiners read it from People)
    std::vector<Question> mQuestions;
    std::vector<SessionEvent> mEvents;
    Clock::time_point mDeadline;                    // a joiner: connect / handshake timeout
    Clock::time_point mNextPeople;                  // the host: next People broadcast

    // Host.
    std::map<LinkId, Link> mLinks;
    std::vector<Pending>   mPending;
    std::map<std::uint8_t, std::uint16_t> mPings;   // id -> ms

    // Shared commands.
    struct LastBegin { std::uint8_t author = kNoId; Clock::time_point at; };
    std::map<std::uint16_t, LastBegin> mLastBegin;                  // host: the last accepted Begin per command
    std::vector<std::pair<std::uint8_t, std::uint16_t>> mHeld;      // host: (author, command) begun, not ended
    std::vector<RemoteCommand> mCommandsIn;
    std::map<std::uint16_t, LastBegin> mLastValue;                  // host: the last accepted change per dataref
    std::map<std::uint16_t, double>    mSessionValues;              // host: the session's value per dataref
    std::vector<RemoteValues>  mValuesIn;
    std::vector<std::uint8_t>  mSnapshotRequests;
    std::vector<std::uint8_t>  mPriority;                           // joiner ids, highest first (everybody keeps a copy)
    SessionRules               mOwnRules;                           // mine (used when I host)
    SessionRules               mRules;                              // in force now
    bool                       mRulesFromHost = false;
    std::map<std::uint8_t, KnownPosition> mPositions;
    std::optional<FlightInputs> mInputsIn;                          // the PIC's newest flight inputs
    std::uint32_t              mInputsSequence = 0;                 // mine, as the PIC
    std::optional<bool>        mPauseIn;                            // the PIC's pause state, arrived
    std::optional<StateIn>     mStateIn;                            // the PIC's newest aircraft state (Follow)
    std::uint32_t              mStateSequence = 0;                  // the PIC: the last state sent
    std::optional<LoadInfo>    mLoadIn;                             // the PIC's fuel and payload, arrived
    std::optional<LoadInfo>    mPicLoad;                            // host: the PIC's last (for new joiners)
    std::vector<SeatInfo>      mOwnSeats;                           // my aircraft's seats
    std::vector<SeatInfo>      mHostSeats;                          // a joiner: the host's
    std::uint8_t               mOwnSeat = kNoSeat;                  // my pick
    bool                       mSeatsChanged = false;
    std::uint16_t              mHostLevers = 0;                     // the host's levers (as Crew)
    std::map<std::uint8_t, LeversIn> mLeversIn;                     // each owner's newest lever positions
    std::optional<CopilotIn>   mCopilotIn;                          // the copilot's newest inputs
    std::uint8_t               mCopilotId = kNoId;                  // host: who has the copilot seat
    Clock::time_point          mCopilotAt;                          // host: when that one last sent
    // Host: true when `id` may have the copilot seat now (and takes it).
    bool HostCopilotSeat_cai(std::uint8_t id);
    std::optional<bool>        mPicPaused;                          // host: the PIC's last pause state (for new joiners)               // everybody's last position (mine too)
    std::optional<std::vector<DataValue>> mWeatherIn;               // joiner: the newest weather from the host
    std::function<std::string(std::uint16_t)> mCommandNamer;
    std::vector<Rejection>     mRejections;

    // Joiner.
    LinkId            mHostLink = kNoLink;
    std::string       mByeText;                     // why the host closed the link (from Bye)
    bool              mOtherProtocol = false;
    std::optional<analysis::AnalysisData> mHostProfile;
};

} // namespace sc::net
