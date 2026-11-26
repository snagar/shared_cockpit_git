// shared_cockpit - sharing the datarefs of the cockpit (step 2, stage 2)
//
// While a session runs, every shared dataref of the session's analysis (or one element of an
// array dataref) is read every frame and compared with its last known value:
//   * changed here by crew:      sent to the host - at most `rate` updates a second while it
//                                keeps moving, and always its final value
//   * changed here by an observer: written back to the session's value (locked)
// Values from the host are written here and become the last known value, so they are not
// sent back (no echoes):
//   * Change:      someone else changed it
//   * Correction:  the host did not share my change (someone ranked higher changed it within
//                  1 s): snap back
//   * Snapshot:    the session's values when I joined
// The host answers each new joiner's snapshot request with the values read here.
// Datarefs not found or not writable here are skipped and listed.

#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "XPLMDataAccess.h"

#include "shared/analysis/Session.h"
#include "shared/net/SessionHub.h"
#include "shared/settings/Settings.h"

namespace sc::sync {

class DatarefSync {
public:
    DatarefSync(analysis::Session& analysis, net::SessionHub& hub, const settings::Settings& settings);
    ~DatarefSync();

    DatarefSync(const DatarefSync&)            = delete;
    DatarefSync& operator=(const DatarefSync&) = delete;

    // From the flight loop (after the hub's poll).
    void Tick_cai();
    // Stops (session ended, plugin disabled).
    void Stop_cai(const char* why);

private:
    enum class Kind { None, Int, Float, Double, IntArray, FloatArray };
    struct Entry {
        std::string  name;
        int          element = -1;
        std::string  label;
        XPLMDataRef  ref = nullptr;
        Kind         kind = Kind::None;
        bool         usable = false;        // found, a type we can read / write, writable
        double       last = 0.0;            // the last known (session) value
        bool         pending = false;       // changed here, not sent yet
        std::chrono::steady_clock::time_point lastSent;
    };

    // Builds the list from the session's analysis and reads the starting values.
    void Start_cai(const analysis::AnalysisData& data);
    // Reads / writes one entry.
    double Read_cai(const Entry& entry) const;
    void Write_cai(const Entry& entry, double value);
    // True when `a` and `b` are different values for this entry.
    static bool Differs_cai(const Entry& entry, double a, double b);
    // "1", "0.75" for the feed.
    static std::string ValueText_cai(const Entry& entry, double value);
    // "Bob: FUEL PUMP ON [1] = 1" (+ the dataref in debug).
    std::string Describe_cai(const std::string& who, const Entry& entry, double value) const;
    // Writes values from the host.
    void Apply_cai(const net::RemoteValues& values);
    // Finds the changes made here, sends them (throttled), reverts them for observers.
    void Scan_cai();
    // Answers the snapshot requests (host).
    void AnswerSnapshots_cai();

    analysis::Session&        mAnalysis;
    net::SessionHub&          mHub;
    const settings::Settings& mSettings;
    std::vector<Entry>        mEntries;
    bool                      mRunning = false;
    bool                      mWasHeld = false;      // the hold (aircraft reload) was on last frame
};

} // namespace sc::sync
