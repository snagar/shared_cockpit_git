// shared_cockpit - the one live session of the plugin, and what happened in it
//
// Owns the NetSession and keeps its events as a short history (the feed) for
// every screen: the Connect page, the Home page and later the flight deck.
// Polled from a flight loop (not from a window), so the session keeps running
// while the windows are closed. Only Leave, the aircraft changing or the plugin
// stopping end it.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <deque>
#include <string>

#include "shared/net/NetSession.h"

namespace sc::net {

struct FeedLine {
    std::string        time;      // local "10:41:07"
    SessionEvent::Kind kind = SessionEvent::Kind::Notice;
    std::string        text;
    std::string        merge;     // lines with the same merge key in a row become one ("x 5")
    int                count = 1;
    std::chrono::steady_clock::time_point at;
};

// What the control sharing (the plugin's sync engine) is doing, for the screens.
struct SyncStats {
    bool                     running = false;
    std::size_t              commands = 0;      // shared commands in the session's analysis
    std::size_t              commandsOk = 0;    // found in this X-Plane
    std::vector<std::string> missing;           // not found here (names)
    std::size_t              datarefs = 0;      // shared datarefs in the session's analysis
    std::size_t              datarefsOk = 0;    // found here and writable
    std::vector<std::string> readOnly;          // found but not writable here (skipped)
    std::uint64_t            sent = 0;          // mine, sent to the others
    std::uint64_t            applied = 0;       // the others', run here
    std::uint64_t            rejected = 0;      // mine, not shared by the host
    std::uint64_t            blocked = 0;       // observer clicks blocked
    std::size_t              perMinute = 0;     // sent + applied in the last minute
    std::string              last;              // "Bob: LANDING LIGHTS ON"
    std::chrono::steady_clock::time_point lastAt;
};

// Where my aircraft is compared with the PIC's, and the move to the PIC (sync::PositionSync).
struct PositionStats {
    enum class Move { None, Moving, Reloading, Settling };
    bool        known = false;          // the PIC's position is known (and I am not the PIC)
    bool        iAmPic = false;
    std::string picName;
    double      distanceM = 0.0;        // to the PIC (3D in the air)
    float       headingOff = 0.0f;      // degrees
    bool        picOnGround = true;
    bool        enginesDiffer = false;  // the PIC's engines run and mine do not (or the other way)
    bool        picEngines = false;     // the PIC's engines run
    bool        off = false;            // farther than the limits: "Move to the PIC" is offered
    bool        ask = false;            // the question after joining is open
    bool        moveRequested = false;  // a screen pressed Move (PositionSync takes it)
    Move        move = Move::None;
    std::string result;                 // "Moved to the PIC (0.4 m)"
    bool        hold = false;           // the aircraft is being reloaded: the sync engines do not send
    bool        reapplyWeather = false; // a move finished: WeatherSync writes the last weather again
    bool        reapplyLoad = false;    // a move finished: LoadSync writes the PIC's last fuel and payload again
    std::chrono::steady_clock::time_point snapshotAt;   // the last snapshot written here
    // Weather and time (sync::WeatherSync).
    bool        weatherFollowing = false;   // a joiner writes the host's weather
    bool        weatherSending = false;     // the host sends its weather
    std::size_t weatherValues = 0;          // in the list, found here
    std::vector<std::string> weatherSkipped;   // not found / not writable here
    std::string weatherSource;              // "real weather" when X-Plane downloads it here (it may overwrite)
    std::chrono::steady_clock::time_point weatherAt;   // the last weather sent / written
};

// Flying together (sync::FlightSync and its mode), for the screens.
struct FlightStats {
    bool        running = false;
    std::string mode;                    // "Inputs" / "Follow"
    bool        iAmPic = false;          // I fly: my inputs go out
    std::size_t engines = 0;             // throttle / prop / mixture elements in use
    std::chrono::steady_clock::time_point inputsAt;   // a follower: the PIC's inputs last arrived
    bool        hardwareOff = false;     // a follower: my own yoke / pedals are switched off
    int         heldPerSecond = 0;       // a follower: frames a second my hardware moved a lever back
    std::vector<std::string> missing;    // datarefs this X-Plane does not have
    bool        picPaused = false;       // the PIC's pause state, as far as known
    // Copilot axes (session rule).
    bool        copilotAvailable = false;  // this X-Plane has the copilot datarefs (12.x)
    bool        iAmCopilot = false;        // my hardware is the copilot side (sent to everybody)
    std::uint8_t copilotId = 0xFF;         // whose copilot inputs I write now (0xFF: none / centred)
    // Follow (session rule).
    bool        following = false;         // a follower: my aircraft is placed at the PIC's now
    bool        holding = false;           // a follower: no state from the PIC for a while - flying on my own
    bool        farAway = false;           // a follower: too far to follow ("Move to PIC" first)
    bool        strict = false;            // a follower: my flight model is off while following
    float       gapM = 0.0f;               // a follower: how far my aircraft was from the PIC's (before the frame's correction)
    float       gapDeg = 0.0f;             //   ... and its heading
    float       delayMs = 0.0f;            // a follower: how late the PIC's state is (age + network delay)
    std::chrono::steady_clock::time_point stateAt;   // a follower: the PIC's state last arrived
    // Fuel and payload (session rule shareLoad).
    std::chrono::steady_clock::time_point loadAt;    // a follower: applied last / the PIC: sent last
    bool        loadStations = false;      // the payload went into the stations (else the total)
    int         loadTanks = 0;             // fuel tanks in it
    // The flight trace ([debug] flight_trace, Home > About).
    bool        traceWriting = false;      // a file is open now
    std::string tracePath;                 // where it goes
    std::uintmax_t traceBytes = 0;         // written so far
};

class SessionHub {
public:
    inline static constexpr std::size_t kFeedSize = 100;

    // Polls the network and moves its events into the feed. Call every frame.
    void Poll_cai();
    // Adds a line of the plugin's own (e.g. "left: the aircraft changed").
    void Note_cai(SessionEvent::Kind kind, std::string text);
    // Adds a line, or - when the last line has the same `merge` key and is less than 3 s old -
    // counts it there ("Bob: HDG UP x 5"), so a turned knob is one line.
    void NoteMerged_cai(SessionEvent::Kind kind, std::string text, const std::string& merge);
    // True for a message someone wrote ("Bob: ready?"), not a session notice (levers, roles, joins).
    bool IsMessage_cai(const std::string& text) const;
    // How many questions / messages from the others arrived so far (the chimes watch these).
    std::uint32_t QuestionCount_cai() const { return mQuestionCount; }
    std::uint32_t NoticeCount_cai() const { return mNoticeCount; }
    // Counts one shared change (sent or run here) for SyncStats::perMinute.
    void CountChange_cai();
    // Ends the session with a reason shown in the feed.
    void Leave_cai(const std::string& why);

    FlightStats&         Flight_cai()         { return mFlight; }
    const FlightStats&   Flight_cai() const   { return mFlight; }
    PositionStats&       Position_cai()       { return mPosition; }
    const PositionStats& Position_cai() const { return mPosition; }
    SyncStats&        Sync_cai()          { return mSync; }
    const SyncStats&  Sync_cai() const    { return mSync; }
    NetSession&       Session_cai()       { return mSession; }
    const NetSession& Session_cai() const { return mSession; }
    const std::deque<FeedLine>& Feed_cai() const { return mFeed; }
    // Grows by one with every feed line (to scroll the feed to the end).
    std::size_t FeedRevision_cai() const { return mFeedRevision; }
    // Grows when the people, the PIC or the questions changed (to redraw / grow windows).
    std::size_t ChangeRevision_cai() const { return mChangeRevision; }

private:
    NetSession           mSession;
    SyncStats            mSync;
    PositionStats        mPosition;
    FlightStats          mFlight;
    std::deque<std::chrono::steady_clock::time_point> mRecentChanges;   // the last minute's changes
    std::uint32_t mQuestionCount = 0, mNoticeCount = 0;
    std::deque<FeedLine> mFeed;
    std::size_t          mFeedRevision = 0;
    std::size_t          mChangeRevision = 0;
};

// One line of a short status: "Hosting · 3 people · PIC Bob", "Not connected", ...
std::string StatusLine_cai(const NetSession& session);

// One status line for the screens: its text and how it is marked.
struct StatusText {
    enum class Mark { Ok, Info, Warn };
    Mark        mark = Mark::Info;
    std::string text;
    std::string tooltip;
};
// Follow (session rule): how my aircraft follows the PIC's ("Following Bob: 0.3 m, 45 ms behind").
// Empty text when the rule is not Follow or nobody else is in the session.
StatusText FollowLine_cai(const NetSession& session, const FlightStats& flight);
// Fuel and payload (session rule shareLoad). Empty text when off or alone.
StatusText LoadLine_cai(const NetSession& session, const FlightStats& flight);

// The sizes of the core's main classes as the core library was compiled ("hub=... session=... settings=...").
// The plugin compares it with its own sizeof()s at start: a mismatch means a stale build (old and new objects mixed).
std::string CoreLayout_cai();

} // namespace sc::net
