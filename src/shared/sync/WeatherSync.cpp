// shared_cockpit - the host's weather and time for everybody (0.6)

#include "shared/sync/WeatherSync.h"

#include <algorithm>
#include <cmath>

#include "fmt/format.h"

#include "shared/log/Log.h"

namespace sc::sync {

namespace {

// The list (protocol 4: never reorder; add at the end with a new protocol version).
struct ListItem { const char* name; int count; bool time; };
constexpr ListItem kList[] = {
    {"sim/weather/region/visibility_reported_sm",   1, false},
    {"sim/weather/region/sealevel_pressure_pas",    1, false},
    {"sim/weather/region/sealevel_temperature_c",   1, false},
    {"sim/weather/region/rain_percent",             1, false},
    {"sim/weather/region/runway_friction",          1, false},
    {"sim/weather/region/variability_pct",          1, false},
    {"sim/weather/region/thermal_rate_ms",          1, false},
    {"sim/weather/region/wind_altitude_msl_m",     13, false},
    {"sim/weather/region/wind_speed_msc",          13, false},
    {"sim/weather/region/wind_direction_degt",     13, false},
    {"sim/weather/region/shear_speed_msc",         13, false},
    {"sim/weather/region/shear_direction_degt",    13, false},
    {"sim/weather/region/turbulence",              13, false},
    {"sim/weather/region/dewpoint_deg_c",          13, false},
    {"sim/weather/region/temperatures_aloft_deg_c", 13, false},
    {"sim/weather/region/cloud_type",               3, false},
    {"sim/weather/region/cloud_coverage_percent",   3, false},
    {"sim/weather/region/cloud_base_msl_m",         3, false},
    {"sim/weather/region/cloud_tops_msl_m",         3, false},
    {"sim/time/zulu_time_sec",                      1, true},
    {"sim/time/local_date_days",                    1, true},
};

} // namespace

WeatherSync::WeatherSync(net::SessionHub& hub, const settings::Settings& settings) : mHub(hub), mSettings(settings)
{
}

// The number of values in the list.
std::size_t WeatherSync::ValueCount_cai()
{
    std::size_t count = 0;
    for (const ListItem& item : kList)
        count += static_cast<std::size_t>(item.count);
    return count;
}

// Finds the datarefs.
void WeatherSync::Start_cai()
{
    mStarted = true;
    mEntries.clear();
    net::PositionStats& stats = mHub.Position_cai();
    stats.weatherSkipped.clear();
    const bool host = mHub.Session_cai().IsHost_cai();
    std::size_t first = 0, found = 0;
    for (const ListItem& item : kList) {
        Entry entry;
        entry.name = item.name;
        entry.count = item.count;
        entry.time = item.time;
        entry.first = first;
        first += static_cast<std::size_t>(item.count);
        entry.ref = XPLMFindDataRef(item.name);
        if (!entry.ref) {
            stats.weatherSkipped.push_back(fmt::format("{} (not found)", item.name));
            mEntries.push_back(entry);
            continue;
        }
        const XPLMDataTypeID types = XPLMGetDataRefTypes(entry.ref);
        if (item.count > 1)
            entry.kind = (types & xplmType_FloatArray) ? Kind::FloatArray : Kind::IntArray;
        else
            entry.kind = (types & xplmType_Float) ? Kind::Float : (types & xplmType_Double) ? Kind::Double : Kind::Int;
        entry.writable = XPLMCanWriteDataRef(entry.ref) != 0;
        if (!host && !entry.writable)
            stats.weatherSkipped.push_back(fmt::format("{} (read-only here)", item.name));
        found += entry.ref && (host || entry.writable) ? static_cast<std::size_t>(item.count) : 0;
        mEntries.push_back(entry);
    }
    mUpdateNow = XPLMFindDataRef("sim/weather/region/update_immediately");
    stats.weatherValues = found;
    if (XPLMDataRef source = XPLMFindDataRef("sim/weather/region/weather_source"))
        LOG_INFO("weather: source here {} (X-Plane's region weather source)", XPLMGetDatai(source));
    LOG_INFO("weather and time: {} {} of {} values{}", host ? "sending" : "following the host (session settings),",
             found, ValueCount_cai(), stats.weatherSkipped.empty() ? "" : fmt::format(", {} datarefs skipped", stats.weatherSkipped.size()));
    for (const std::string& name : stats.weatherSkipped)
        LOG_INFO("  weather skipped: {}", name);
}

// Reads one entry.
void WeatherSync::Read_cai(const Entry& entry, std::vector<double>& out) const
{
    if (!entry.ref)
        return;
    switch (entry.kind) {
        case Kind::Int:    out[entry.first] = XPLMGetDatai(entry.ref); break;
        case Kind::Float:  out[entry.first] = XPLMGetDataf(entry.ref); break;
        case Kind::Double: out[entry.first] = XPLMGetDatad(entry.ref); break;
        case Kind::FloatArray: {
            float values[16] = {};
            const int n = XPLMGetDatavf(entry.ref, values, 0, std::min(entry.count, 16));
            for (int i = 0; i < n; ++i)
                out[entry.first + static_cast<std::size_t>(i)] = values[i];
            break;
        }
        case Kind::IntArray: {
            int values[16] = {};
            const int n = XPLMGetDatavi(entry.ref, values, 0, std::min(entry.count, 16));
            for (int i = 0; i < n; ++i)
                out[entry.first + static_cast<std::size_t>(i)] = values[i];
            break;
        }
    }
}

// Writes one entry.
void WeatherSync::Write_cai(const Entry& entry, const std::vector<double>& values) const
{
    if (!entry.ref || !entry.writable)
        return;
    const double v = values[entry.first];
    switch (entry.kind) {
        case Kind::Int:    XPLMSetDatai(entry.ref, static_cast<int>(std::lround(v))); break;
        case Kind::Float:  XPLMSetDataf(entry.ref, static_cast<float>(v)); break;
        case Kind::Double: XPLMSetDatad(entry.ref, v); break;
        case Kind::FloatArray: {
            float out[16];
            for (int i = 0; i < entry.count && i < 16; ++i)
                out[i] = static_cast<float>(values[entry.first + static_cast<std::size_t>(i)]);
            XPLMSetDatavf(entry.ref, out, 0, std::min(entry.count, 16));
            break;
        }
        case Kind::IntArray: {
            int out[16];
            for (int i = 0; i < entry.count && i < 16; ++i)
                out[i] = static_cast<int>(std::lround(values[entry.first + static_cast<std::size_t>(i)]));
            XPLMSetDatavi(entry.ref, out, 0, std::min(entry.count, 16));
            break;
        }
    }
}

// The host: sends to a new joiner at once, every weatherS, and on a change (at most every kWeatherMinGap).
void WeatherSync::HostTick_cai()
{
    const net::NetSession& session = mHub.Session_cai();
    const net::SessionRules& rules = session.Rules_cai();
    if (!rules.weather && !rules.time)
        return;                                       // the session does not share them
    const auto now = Clock::now();
    // A new joiner: right away (it got the snapshot just now).
    bool newcomer = false;
    std::vector<std::uint8_t> ids;
    for (const net::Person& p : session.People_cai())
        if (p.id != net::kHostId) {
            ids.push_back(p.id);
            newcomer = newcomer || std::find(mKnownIds.begin(), mKnownIds.end(), p.id) == mKnownIds.end();
        }
    mKnownIds = ids;
    if (!newcomer && now < mNextCheck)
        return;
    mNextCheck = now + std::chrono::seconds(1);
    std::vector<double> values(ValueCount_cai(), 0.0);
    for (const Entry& entry : mEntries)
        Read_cai(entry, values);
    bool changed = false;
    if (mLastSent.size() == values.size()) {
        for (const Entry& entry : mEntries) {
            if (changed || entry.time)
                continue;
            for (int i = 0; i < entry.count && !changed; ++i) {
                const std::size_t at = entry.first + static_cast<std::size_t>(i);
                changed = std::fabs(values[at] - mLastSent[at]) > 1e-3 * std::max(1.0, std::fabs(values[at]));
            }
        }
    }
    const bool due = now >= mNextSend;
    const bool changeAllowed = changed && now - mLastSendAt >= std::chrono::seconds(static_cast<int>(net::kWeatherMinGap));
    if (ids.empty() || (!newcomer && !due && !changeAllowed))
        return;
    LOG_DEBUG("weather: sending ({})", newcomer ? "a new joiner" : changeAllowed ? "it changed here" : "on schedule");
    mNextSend = now + std::chrono::seconds(rules.weatherS);
    mLastSendAt = now;
    std::vector<net::DataValue> out;
    out.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i)
        out.push_back({static_cast<std::uint16_t>(i), values[i]});
    mHub.Session_cai().SendWeather_cai(out);
    mLastSent = std::move(values);
    mHub.Position_cai().weatherAt = now;
}

// Writes the host's values here: the weather when the session shares it, the clock when it is off.
void WeatherSync::Apply_cai(const std::vector<double>& values, const std::vector<bool>& have, double clockAhead, const char* why)
{
    const net::SessionRules& rules = mHub.Session_cai().Rules_cai();
    TakeOver_cai(rules);
    std::vector<double> here(values.size(), 0.0);
    std::vector<double> adjusted = values;
    std::size_t written = 0;
    for (const Entry& entry : mEntries) {
        if (!have[entry.first] || !entry.writable)
            continue;
        if (entry.time) {
            if (!rules.time)
                continue;
            const bool clock = std::string_view(entry.name) == "sim/time/zulu_time_sec";
            if (clock)
                adjusted[entry.first] = std::fmod(values[entry.first] + clockAhead, 86400.0);
            Read_cai(entry, here);
            const double off = std::fabs(here[entry.first] - adjusted[entry.first]);
            if (off <= (clock ? static_cast<double>(rules.timeToleranceS) : 0.5))
                continue;
            LOG_INFO("time: {} set to the host's ({} -> {}{})", entry.name, here[entry.first], adjusted[entry.first], why);
        } else if (!rules.weather) {
            continue;
        }
        Write_cai(entry, adjusted);
        ++written;
    }
    if (rules.weather && mUpdateNow && XPLMCanWriteDataRef(mUpdateNow))
        XPLMSetDatai(mUpdateNow, 1);
    net::PositionStats& stats = mHub.Position_cai();
    if (stats.weatherAt == Clock::time_point{} || why[0] != '\0')
        LOG_INFO("weather: the host's weather and time written ({} datarefs{})", written, why);
    stats.weatherAt = Clock::now();
}

// A joiner: real weather and the system clock would overwrite the host's: kept off here for the
// whole session (checked every 15 s; X-Plane or the user may switch them on again), back after.
// X-Plane 12: sim/weather/region/change_mode 7 = "Using Real Weather", 0..6 = not real weather
// (3 = static), then update_immediately (the way Mission-X does it). X-Plane 11: use_real_weather_bool.
void WeatherSync::TakeOver_cai(const net::SessionRules& rules)
{
    struct Off { const char* name; int on; int off; };   // `on`: the value that means "on" (-1: any but 0)
    auto lmbda_off = [this](Switch& sw, bool wanted, std::initializer_list<Off> names, const char* what) {
        if (!wanted)
            return;
        for (const Off& o : names) {
            XPLMDataRef ref = XPLMFindDataRef(o.name);
            if (!ref)
                continue;
            const int now = XPLMGetDatai(ref);
            if (o.on >= 0 ? now != o.on : now == 0)
                return;                               // off (the first one X-Plane has decides)
            if (!XPLMCanWriteDataRef(ref)) {
                if (!sw.warned)
                    LOG_WARN("{}: {} is {} and X-Plane does not let the plugin change it", what, o.name, now);
                sw.warned = true;
                return;
            }
            XPLMSetDatai(ref, o.off);
            if (&sw == &mRealWeather && mUpdateNow && XPLMCanWriteDataRef(mUpdateNow))
                XPLMSetDatai(mUpdateNow, 1);
            const int after = XPLMGetDatai(ref);
            if (!sw.changed)
                sw = Switch{ref, now, true, sw.warned};
            if (after == now) {
                if (!sw.warned)
                    LOG_WARN("{}: {} {} -> {} was not taken by X-Plane (still {})", what, o.name, now, o.off, after);
                sw.warned = true;
            } else {
                ++sw.count;
                if (sw.count <= 3 || sw.count % 20 == 0)
                    LOG_INFO("{}: switched off here while following the host ({} {} -> {}, {} time(s); back when the session ends)",
                             what, o.name, now, o.off, sw.count);
            }
            return;
        }
    };
    lmbda_off(mRealWeather, rules.weather,
              {{"sim/weather/region/change_mode", 7, 3}, {"sim/weather/use_real_weather_bool", -1, 0}}, "real weather");
    lmbda_off(mSystemTime, rules.time, {{"sim/time/use_system_time", -1, 0}}, "the system clock");
}

// Real weather and the system clock back as they were.
void WeatherSync::GiveBack_cai()
{
    for (Switch* sw : {&mRealWeather, &mSystemTime}) {
        if (sw->changed && sw->ref) {
            XPLMSetDatai(sw->ref, sw->before);
            if (sw == &mRealWeather && mUpdateNow && XPLMCanWriteDataRef(mUpdateNow))
                XPLMSetDatai(mUpdateNow, 1);              // real weather again now
            LOG_INFO("weather and time: {} back to {}", sw == &mRealWeather ? "real weather" : "the system clock", sw->before);
        }
        *sw = Switch{};
    }
}

// A joiner: writes the host's newest weather; after a move the last one again.
void WeatherSync::JoinTick_cai()
{
    const net::SessionRules& rules = mHub.Session_cai().Rules_cai();
    net::PositionStats& stats = mHub.Position_cai();
    if (const auto weather = mHub.Session_cai().TakeWeather_cai()) {
        mLast.assign(ValueCount_cai(), 0.0);
        mHave.assign(mLast.size(), false);
        for (const net::DataValue& v : *weather)
            if (v.index < mLast.size()) {
                mLast[v.index] = v.value;
                mHave[v.index] = true;
            }
        mLastAt = Clock::now();
        if (rules.weather || rules.time)
            Apply_cai(mLast, mHave, 0.0, "");
    }
    if (const Clock::time_point now = Clock::now(); now >= mNextTakeOver) {   // real weather stays off
        mNextTakeOver = now + std::chrono::seconds(15);
        TakeOver_cai(rules);
    }
    if (stats.reapplyWeather) {
        stats.reapplyWeather = false;
        if (!mLast.empty() && (rules.weather || rules.time))
            Apply_cai(mLast, mHave, std::chrono::duration<double>(Clock::now() - mLastAt).count(), ", again after the move");
    }
}

// From the flight loop.
void WeatherSync::Tick_cai()
{
    net::NetSession& session = mHub.Session_cai();
    net::PositionStats& stats = mHub.Position_cai();
    if (!session.Active_cai()) {
        GiveBack_cai();
        if (mStarted)
            LOG_INFO("weather and time: stopped");
        mStarted = false;
        mLastSent.clear();
        mLast.clear();
        mHave.clear();
        mKnownIds.clear();
        stats.weatherFollowing = stats.weatherSending = false;
        stats.weatherAt = {};
        session.TakeWeather_cai();
        return;
    }
    if (!mStarted)
        Start_cai();
    const net::SessionRules& rules = session.Rules_cai();
    stats.weatherSending = session.IsHost_cai() && (rules.weather || rules.time);
    stats.weatherFollowing = !session.IsHost_cai() && (rules.weather || rules.time);
    if (session.IsHost_cai())
        HostTick_cai();
    else
        JoinTick_cai();
}

} // namespace sc::sync
