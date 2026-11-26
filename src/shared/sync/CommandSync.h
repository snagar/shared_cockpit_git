// shared_cockpit - sharing the commands of the cockpit (step 2, stage 1)
//
// While a session runs, every shared command of the session's analysis gets an X-Plane
// command handler (before X-Plane's own). A press here:
//   * crew:      runs here as usual, and its Begin / End go to the host, which sends them on
//   * observer:  is blocked (the handler consumes it) - observers touch nothing
// A press from someone else is run here with XPLMCommandBegin / XPLMCommandEnd (a held
// button stays held), marked as "applying" so the handler lets it through and does not
// send it back (no echoes).
// Commands that are not found in this X-Plane (another aircraft version) are skipped and
// listed. When the session ends, every command still held for someone else is ended.
// The only code here that uses X-Plane; the rules are in shared/net/NetSession.

#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "XPLMUtilities.h"

#include "shared/analysis/Session.h"
#include "shared/net/SessionHub.h"

namespace sc::sync {

class CommandSync {
public:
    CommandSync(analysis::Session& analysis, net::SessionHub& hub);
    ~CommandSync();

    CommandSync(const CommandSync&)            = delete;
    CommandSync& operator=(const CommandSync&) = delete;

    // From the flight loop (after the hub's poll): starts / stops with the session, runs the
    // others' commands, counts.
    void Tick_cai();
    // Unregisters everything and ends the commands held for others (plugin disabled).
    void Stop_cai(const char* why);

private:
    struct Entry {
        std::string    name;
        std::string    label;
        XPLMCommandRef ref = nullptr;
        bool           remoteHeld = false;     // begun here for someone else
        std::uint8_t   remoteAuthor = 0;
        bool           localHeld = false;      // pressed here, Begin sent
        bool           blockedHeld = false;    // pressed here by an observer, consumed
    };
    struct Refcon {
        CommandSync* self = nullptr;
        std::size_t  index = 0;
    };

    // X-Plane's handler for every shared command (refcon = Refcon*).
    static int Handler_cai(XPLMCommandRef command, XPLMCommandPhase phase, void* refcon);
    // A phase of shared command `index`. 1 = X-Plane goes on, 0 = consumed.
    int OnCommand_cai(std::size_t index, XPLMCommandPhase phase);
    // Builds the list from the session's analysis and registers the handlers.
    void Start_cai(const analysis::AnalysisData& data);
    // Runs one command of someone else.
    void Apply_cai(const net::RemoteCommand& command);
    // "Bob: LANDING LIGHTS ON" (+ the command in debug).
    std::string Describe_cai(const std::string& who, const Entry& entry) const;
    // Counts one change for the per-minute figure.
    void Count_cai();

    analysis::Session& mAnalysis;
    net::SessionHub&   mHub;
    std::vector<Entry> mEntries;
    std::vector<std::unique_ptr<Refcon>> mRefcons;
    bool               mRunning = false;
    bool               mApplying = false;       // XPLMCommandBegin / End of a remote command right now
    std::chrono::steady_clock::time_point mNextSummary;
};

} // namespace sc::sync
