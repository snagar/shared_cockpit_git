// shared_cockpit - the plugin's own settings (shared_cockpit.ini in the plugin folder)

#include "shared/settings/Settings.h"

#include <algorithm>

#include "fmt/format.h"

#include "shared/log/Log.h"
#include "shared/util/Utils.h"

namespace sc::settings {

namespace {

// A port from text, or `fallback`.
std::uint16_t PortOr_cai(std::string_view text, std::uint16_t fallback)
{
    int value = 0;
    return Utils::ParseInt_cai(text, value) && value > 0 && value < 65536 ? static_cast<std::uint16_t>(value) : fallback;
}

} // namespace

// Reads the file (missing = defaults).
bool Settings::Load_cai(const std::filesystem::path& file, std::string& error)
{
    mFile = file;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        LOG_INFO("no settings file yet ({}), using the defaults", Utils::Utf8_cai(file));
        return true;
    }
    const std::string text = Utils::ReadAll_cai(file);
    if (text.empty()) {
        error = fmt::format("cannot read {}", Utils::Utf8_cai(file));
        LOG_WARN("{}", error);
        return false;
    }
    std::string section;
    ConnectSettings read;
    FlightDeckSettings deck;
    net::SessionRules rule;
    DebugSettings dbg;
    for (const std::string& raw : Utils::Split_cai(text, '\n')) {
        std::string_view line = Utils::Trim_cai(raw);
        if (const std::size_t comment = line.find(';'); comment != std::string_view::npos)
            line = Utils::Trim_cai(line.substr(0, comment));
        if (line.empty() || line[0] == '#')
            continue;
        if (line.front() == '[' && line.back() == ']') {
            section = Utils::Lower_cai(line.substr(1, line.size() - 2));
            continue;
        }
        const std::size_t equal = line.find('=');
        if (equal == std::string_view::npos)
            continue;
        const std::string key = Utils::Lower_cai(Utils::Trim_cai(line.substr(0, equal)));
        const std::string value(Utils::Trim_cai(line.substr(equal + 1)));
        if (section == "debug") {
            if (key == "flight_trace")
                dbg.flightTrace = value != "0" && Utils::Lower_cai(value) != "false" && Utils::Lower_cai(value) != "no";
            continue;
        }
        if (section == "flightdeck") {
            int number = 0;
            if (key == "size")
                deck.size = value == "minimal" ? 0 : value == "line" ? 1 : 2;
            else if (key == "left" && Utils::ParseInt_cai(value, number))
                deck.left = number;
            else if (key == "top" && Utils::ParseInt_cai(value, number))
                deck.top = number;
            else if (key == "opacity" && Utils::ParseInt_cai(value, number))
                deck.opacity = std::clamp(number, 20, 100);
            continue;
        }
        if (section == "session" || section == "position" || section == "sharing") {
            // [session]; the 0.5 / 0.6 keys of [sharing] and [position] too (the next save writes [session]).
            int number = 0;
            auto lmbda_bool = [&value]() { return value != "0" && Utils::Lower_cai(value) != "false" && Utils::Lower_cai(value) != "no"; };
            if (key == "rate" && Utils::ParseInt_cai(value, number))
                rule.rate = number;
            else if (key == "conflict_ms" && Utils::ParseInt_cai(value, number))
                rule.conflictMs = number;
            else if (key == "ground_m" && Utils::ParseInt_cai(value, number))
                rule.groundM = number;
            else if (key == "heading_deg" && Utils::ParseInt_cai(value, number))
                rule.headingDeg = number;
            else if (key == "air_m" && Utils::ParseInt_cai(value, number))
                rule.airM = number;
            else if (key == "ask_on_join")
                rule.askOnJoin = lmbda_bool();
            else if (key == "weather")
                rule.weather = lmbda_bool();
            else if (key == "weather_s" && Utils::ParseInt_cai(value, number))
                rule.weatherS = number;
            else if (key == "time")
                rule.time = lmbda_bool();
            else if (key == "time_tolerance_s" && Utils::ParseInt_cai(value, number))
                rule.timeToleranceS = number;
            else if (key == "copilot_axes")
                rule.copilotAxes = lmbda_bool();
            else if (key == "fly_together")       // 0.9; the 0.8 key "flight" is ignored (it could only be "inputs")
                rule.flight = Utils::Lower_cai(value) == "inputs" ? net::FlightMode::Inputs : net::FlightMode::Follow;
            else if (key == "smoothing_ms" && Utils::ParseInt_cai(value, number))
                rule.smoothingMs = number;
            else if (key == "follow_strict")
                rule.followStrict = lmbda_bool();
            else if (key == "share_load")
                rule.shareLoad = lmbda_bool();
            else if (key == "handover_blend_ms" && Utils::ParseInt_cai(value, number))
                rule.handoverBlendMs = number;
            continue;
        }
        if (section != "connect")
            continue;
        if (key == "name")
            read.name = value;
        else if (key == "mode")
            read.hostMode = Utils::Lower_cai(value) == "host";
        else if (key == "join_port")
            read.joinPort = PortOr_cai(value, read.joinPort);
        else if (key == "host_port")
            read.hostPort = PortOr_cai(value, read.hostPort);
        else if (key == "max_people") {
            int max = 0;
            if (Utils::ParseInt_cai(value, max))
                read.maxPeople = std::clamp(max, 2, 4);
        } else if (key == "joiners_start_as")
            read.joinersObserve = Utils::Lower_cai(value) == "observer";
        else if (key == "recent" && !value.empty() && read.recent.size() < kMaxRecent)
            read.recent.push_back(value);
    }
    connect = read;
    flightDeck = deck;
    rule.Clamp_cai();
    rules = rule;
    debug = dbg;
    LOG_INFO("settings read: name '{}', mode {}, {} recent address(es)", connect.name, connect.hostMode ? "host" : "join",
             connect.recent.size());
    return true;
}

// Writes the file.
bool Settings::Save_cai(std::string& error) const
{
    if (mFile.empty()) {
        error = "no settings file";
        return false;
    }
    std::string text = "; Shared Cockpit settings - written by the plugin. Passwords are never saved.\n[connect]\n";
    text += fmt::format("name={}\nmode={}\njoin_port={}\nhost_port={}\nmax_people={}\njoiners_start_as={}\n", connect.name,
                        connect.hostMode ? "host" : "join", connect.joinPort, connect.hostPort, connect.maxPeople,
                        connect.joinersObserve ? "observer" : "crew");
    for (const std::string& address : connect.recent)
        text += fmt::format("recent={}\n", address);
    static constexpr const char* kSizes[] = {"minimal", "line", "small"};
    text += fmt::format("\n[flightdeck]\nsize={}\nleft={}\ntop={}\nopacity={}\n", kSizes[std::clamp(flightDeck.size, 0, 2)],
                        flightDeck.left, flightDeck.top, flightDeck.opacity);
    text += fmt::format("\n[session]\nrate={}\nconflict_ms={}\nground_m={}\nheading_deg={}\nair_m={}\nask_on_join={}\n"
                        "weather={}\nweather_s={}\ntime={}\ntime_tolerance_s={}\nfly_together={}\nsmoothing_ms={}\n"
                        "follow_strict={}\nshare_load={}\nhandover_blend_ms={}\ncopilot_axes={}\n",
                        rules.rate, rules.conflictMs, rules.groundM, rules.headingDeg, rules.airM, rules.askOnJoin ? 1 : 0,
                        rules.weather ? 1 : 0, rules.weatherS, rules.time ? 1 : 0, rules.timeToleranceS,
                        rules.flight == net::FlightMode::Follow ? "follow" : "inputs", rules.smoothingMs,
                        rules.followStrict ? 1 : 0, rules.shareLoad ? 1 : 0, rules.handoverBlendMs, rules.copilotAxes ? 1 : 0);
    text += fmt::format("\n[debug]\nflight_trace={}\n", debug.flightTrace ? 1 : 0);
    if (!Utils::WriteFileSafely_cai(mFile, text, error)) {
        LOG_WARN("cannot save the settings: {}", error);
        return false;
    }
    LOG_DEBUG("settings saved to {}", Utils::Utf8_cai(mFile));
    return true;
}

// Puts `address` first in the recent list.
void Settings::AddRecent_cai(const std::string& address)
{
    const std::string clean(Utils::Trim_cai(address));
    if (clean.empty())
        return;
    auto lmbda_same = [&clean](const std::string& a) { return Utils::Lower_cai(a) == Utils::Lower_cai(clean); };
    std::erase_if(connect.recent, lmbda_same);
    connect.recent.insert(connect.recent.begin(), clean);
    if (connect.recent.size() > kMaxRecent)
        connect.recent.resize(kMaxRecent);
}

} // namespace sc::settings
