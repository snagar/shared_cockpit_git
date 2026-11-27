// shared_cockpit - the host's weather and time for everybody (0.6)
//
// The host reads a fixed list of X-Plane 12 weather datarefs (sim/weather/region/...) and the
// time and date, and sends them to a new joiner at once, every 10 / 30 / 60 s (session rule,
// default 60) and when the weather changes (at most every 10 s). Joiners write them as the
// session's rules say (weather, time); they never send weather back; after a move they write the
// last weather again. The clock is written only when it is off by more than the tolerance (30 s). The list's order is part of the protocol:
// a value travels as its place in the list. Datarefs not found or not writable here are
// skipped and listed. A joiner's real weather and "use system time" would overwrite them, so
// while the session shares weather / time they are switched off here and back on when the
// session ends (0.9.7); checked every 15 s, as X-Plane or the user may switch them on (0.9.8);
// X-Plane 12 real weather: sim/weather/region/change_mode 7 (0.9.10).

#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "XPLMDataAccess.h"

#include "shared/net/SessionHub.h"
#include "shared/settings/Settings.h"

namespace sc::sync {

class WeatherSync {
public:
    WeatherSync(net::SessionHub& hub, const settings::Settings& settings);

    WeatherSync(const WeatherSync&)            = delete;
    WeatherSync& operator=(const WeatherSync&) = delete;

    // From the flight loop.
    void Tick_cai();

    // The number of values in the list (both sides must agree).
    static std::size_t ValueCount_cai();

private:
    using Clock = std::chrono::steady_clock;
    enum class Kind { Int, Float, Double, IntArray, FloatArray };
    struct Entry {
        const char*  name = "";
        int          count = 1;                 // values in the list (array elements)
        bool         time = false;              // the clock: not a "weather changed" reason
        std::size_t  first = 0;                 // its first index in the list
        XPLMDataRef  ref = nullptr;
        Kind         kind = Kind::Float;
        bool         writable = false;
    };

    // Finds the datarefs (once per session).
    void Start_cai();
    // Reads one entry into `out` (at entry.first ...).
    void Read_cai(const Entry& entry, std::vector<double>& out) const;
    // Writes one entry from the host's values.
    void Write_cai(const Entry& entry, const std::vector<double>& values) const;
    // The host: to a new joiner at once, every weatherS (session rule), on a change (at most every 10 s).
    void HostTick_cai();
    // A joiner: writes the host's newest weather; after a move the last one again.
    void JoinTick_cai();
    // Writes the host's values (the weather / the clock as the session's rules say). `clockAhead`:
    // seconds to add to the host's clock (the age of the values); `why` goes into the log line.
    void Apply_cai(const std::vector<double>& values, const std::vector<bool>& have, double clockAhead, const char* why);

    net::SessionHub&          mHub;
    const settings::Settings& mSettings;
    std::vector<Entry>        mEntries;
    bool                      mStarted = false;
    std::vector<double>       mLastSent;                // host: what went out last
    Clock::time_point         mNextCheck, mNextSend, mLastSendAt;
    std::vector<std::uint8_t> mKnownIds;                // host: the joiners already sent to
    std::vector<double>       mLast;                    // joiner: the host's last weather (for after a move)
    std::vector<bool>         mHave;
    Clock::time_point         mLastAt;
    XPLMDataRef               mUpdateNow = nullptr;     // sim/weather/region/update_immediately
    // A joiner: real weather and the system clock off while the host's are followed; back after.
    void TakeOver_cai(const net::SessionRules& rules);
    void GiveBack_cai();
    struct Switch { XPLMDataRef ref = nullptr; int before = 0; bool changed = false; bool warned = false; int count = 0; };
    Switch                    mRealWeather;             // sim/weather/region/change_mode (XP12) or use_real_weather_bool (XP11)
    Switch                    mSystemTime;              // sim/time/use_system_time
    Clock::time_point         mNextTakeOver{};          // a joiner: the next real weather / clock check
};

} // namespace sc::sync
