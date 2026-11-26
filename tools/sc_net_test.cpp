// shared_cockpit - network test tool (no X-Plane needed)
//
// Hosts or joins a Shared Cockpit session from a console menu, with the same
// network code as the plugin (shared/net). Two or more people can run it on
// their own computers (LAN, a forwarded port, ZeroTier or Tailscale) and try the
// connection, the password, the seats and the control handover.
//
//   sc_net_test                          the menu
//   sc_net_test --name Bob --join 192.168.1.20 --password abc
//   sc_net_test --name Ann --host --password abc --profile Cessna_172SP.shared_cockpit.json
//   sc_net_test --name Bob --join 192.168.1.20 --aircraft "/X-Plane 12/Aircraft/Laminar Research/Cessna 172 SP"
//   sc_net_test --selftest               host + joiners in this process, checks every step
//
// Options: --aircraft <file.acf | folder>  --name <n>  --acf <file.acf>  --stamp <s>  --profile <*.shared_cockpit.json>
//          --host [port]  --join <address[:port]>  --port <p>  --password <p>
//          --observer (host: joiners start as observers)  --max <2..4>  --debug  --quiet
// Logs: the console (Info, or Debug with --debug) and sc_net_test.log in the current folder.
// Settings: sc_net_test.ini next to the tool keeps what was typed last time (key=value; no
// passwords). The command line wins; --fresh starts from the defaults, --config <file> uses
// another file (e.g. one per test person).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "fmt/format.h"

#include "SharedInfo.h"
#include "shared/analysis/Analysis.h"
#include "shared/log/Log.h"
#include "shared/net/NetSession.h"
#include "shared/store/AnalysisStore.h"
#include "shared/util/Utils.h"

namespace fs = std::filesystem;
using namespace sc;
using namespace sc::net;

namespace {

const std::string kToolVersion = fmt::format("{}.{}", info::kVersion, info::kBuild);

std::mutex gPrintMutex;
std::atomic<bool> gConsoleLog{true};

// Prints a line to the console (whole lines, also from the log).
void Print_cai(const std::string& text)
{
    std::lock_guard lock(gPrintMutex);
    std::fputs(text.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// The log's mirror: log lines go to the console too.
void ConsoleSink_cai(const char* line)
{
    if (!gConsoleLog.load())
        return;
    std::lock_guard lock(gPrintMutex);
    std::fputs("  log| ", stdout);
    std::fputs(line, stdout);
    std::fflush(stdout);
}

// ======================================================================================
//  Profiles
// ======================================================================================

// A small made-up analysis, so the tool works without an aircraft. `variant` 1 shares
// one more control (to try "the host's profile differs").
analysis::AnalysisData DemoAnalysis_cai(const std::string& acfName, const std::string& stamp, int variant)
{
    analysis::AnalysisData data;
    data.format = info::kAnalysisFormat;
    data.pluginVersion = kToolVersion;
    data.analysed = Utils::NowIso_cai();
    data.aircraftName = "Demo aircraft";
    data.acf = acfName;
    data.acfStamp = stamp;
    analysis::TypeGroup commands;
    commands.type = "command";
    commands.category = analysis::Category::Command;
    for (const char* ref : {"sim/electrical/battery_1_on", "sim/electrical/battery_1_off", "sim/lights/landing_lights_toggle",
                            "sim/fuel/fuel_pump_1_on", "sim/engines/mixture_up"}) {
        analysis::Control control;
        control.refs = {ref};
        control.share = true;
        control.file = "cockpit.obj";
        commands.items.push_back(control);
    }
    if (variant == 1)
        commands.items.back().share = false;
    commands.count = commands.items.size();
    data.controls.push_back(commands);
    analysis::TypeGroup axis;
    axis.type = "drag_axis";
    axis.category = analysis::Category::Dataref;
    analysis::Control throttle;
    throttle.refs = {"sim/cockpit2/engine/actuators/throttle_ratio_all"};
    throttle.share = true;
    throttle.file = "cockpit.obj";
    axis.items.push_back(throttle);
    axis.count = 1;
    data.controls.push_back(axis);
    return data;
}

// Fills the profile part of `local` from an analysis.
void UseAnalysis_cai(LocalInfo& local, const analysis::AnalysisData& data)
{
    const auto store = store::MakeAnalysisStore_cai();
    local.profileText = store->ToText_cai(data);
    local.profileHash = analysis::ProfileHash_cai(data);
    local.allKeys = analysis::AllKeys_cai(data);
}

// Loads a *.shared_cockpit.json file as the profile (and takes its aircraft name and stamp).
bool LoadProfile_cai(LocalInfo& local, const std::string& file, std::string& error)
{
    const std::string text = Utils::ReadAll_cai(Utils::PathFromUtf8_cai(file));
    if (text.empty()) {
        error = fmt::format("cannot read '{}'", file);
        return false;
    }
    const auto store = store::MakeAnalysisStore_cai();
    analysis::AnalysisData data;
    if (!store->FromText_cai(text, data, error))
        return false;
    local.profileText = text;
    local.profileHash = analysis::ProfileHash_cai(data);
    local.allKeys = analysis::AllKeys_cai(data);
    if (!data.acf.empty())
        local.acfName = data.acf;
    if (!data.acfStamp.empty())
        local.acfStamp = data.acfStamp;
    LOG_INFO("profile '{}': aircraft '{}', {} controls, {} shared, hash {}", file, data.acf, data.ControlCount_cai(),
             data.SharedCount_cai(), local.profileHash);
    return true;
}

// Uses an aircraft, like the plugin's step 1 result: `path` is the .acf file or its folder.
// The .acf name and stamp are worked out exactly as the plugin does (the stamp is the hash
// of the .acf content), and <acf>.shared_cockpit.json next to it is the profile. Without that file the
// tool can still join (the host's analysis is used) but not host.
bool LoadAircraft_cai(LocalInfo& local, const std::string& path, std::string& error)
{
    std::error_code ec;
    fs::path acf = Utils::PathFromUtf8_cai(std::string(Utils::Trim_cai(path)));
    if (fs::is_directory(acf, ec)) {
        std::vector<fs::path> found;
        for (const auto& entry : fs::directory_iterator(acf, ec))
            if (entry.is_regular_file(ec) && Utils::Lower_cai(Utils::Utf8_cai(entry.path().extension())) == ".acf")
                found.push_back(entry.path());
        if (found.empty()) {
            error = fmt::format("no .acf file in '{}'", path);
            return false;
        }
        if (found.size() > 1) {
            std::string names;
            for (const fs::path& f : found)
                names += fmt::format("{}{}", names.empty() ? "" : ", ", Utils::Utf8_cai(f.filename()));
            error = fmt::format("'{}' has {} .acf files ({}): give the .acf file itself", path, found.size(), names);
            return false;
        }
        acf = found.front();
    }
    if (!fs::is_regular_file(acf, ec)) {
        error = fmt::format("'{}' is not a .acf file or an aircraft folder", path);
        return false;
    }
    const std::string acfText = Utils::ReadAll_cai(acf);
    if (acfText.empty()) {
        error = fmt::format("cannot read '{}'", Utils::Utf8_cai(acf));
        return false;
    }
    local.acfName = Utils::Utf8_cai(acf.filename());
    local.acfStamp = Utils::Hash_cai(acfText);         // the same stamp as the plugin
    const auto store = store::MakeAnalysisStore_cai();
    const fs::path json = store::AnalysisPathFor_cai(acf, *store);
    local.profileText.clear();
    local.profileHash.clear();
    local.allKeys.clear();
    if (!fs::exists(json, ec)) {
        LOG_WARN("aircraft '{}' (stamp {}): no {} - analyse and save it in the plugin first (step 1). "
                 "You can join, not host.", local.acfName, local.acfStamp, Utils::Utf8_cai(json.filename()));
        Print_cai(fmt::format("  no {} next to the .acf: you can join (the host's analysis is used), not host",
                              Utils::Utf8_cai(json.filename())));
        return true;
    }
    const std::string acfName = local.acfName, acfStamp = local.acfStamp;
    if (!LoadProfile_cai(local, Utils::Utf8_cai(json), error))
        return false;
    local.acfName = acfName;        // the real file, not what the analysis remembers
    local.acfStamp = acfStamp;
    LOG_INFO("aircraft '{}' (stamp {}), analysis {}", local.acfName, local.acfStamp, Utils::Utf8_cai(json));
    return true;
}

// A LocalInfo with the demo profile.
LocalInfo DemoLocal_cai(const std::string& name, const std::string& acf, const std::string& stamp, int variant)
{
    LocalInfo local;
    local.name = name;
    local.pluginVersion = kToolVersion;
    local.acfName = acf;
    local.acfStamp = stamp;
    UseAnalysis_cai(local, DemoAnalysis_cai(acf, stamp, variant));
    return local;
}

// ======================================================================================
//  Printing the session
// ======================================================================================

// "Crew, PIC" etc. for one person.
std::string PersonLine_cai(const NetSession& session, const Person& p)
{
    std::string warnings;
    if (p.flags & kFlagAcfVersion)
        warnings += " acf-version";
    if (p.flags & kFlagHostProfile)
        warnings += " host-profile";
    if (p.flags & kFlagMissingControls)
        warnings += " missing-controls";
    return fmt::format("   {:>2}  {:<16} {:<9} {:<4} {:<7} {:>5}  {}{}", p.id, p.name, RoleName_cai(p.role),
                       p.pic ? "PIC" : "", p.master ? "master" : "", p.id == kHostId ? std::string("-") : fmt::format("{}ms", p.pingMs),
                       warnings.empty() ? "-" : warnings.substr(1), p.id == session.MyId_cai() ? "   <- you" : "");
}

// Prints the state, the people and the open questions.
void PrintSession_cai(const NetSession& session)
{
    Print_cai(fmt::format("  state: {}{}", StateName_cai(session.State_cai()),
                          session.FailText_cai().empty() || session.Active_cai() ? std::string()
                                                                                  : " (" + session.FailText_cai() + ")"));
    if (session.People_cai().empty())
        return;
    Print_cai("   id  name             seat      ctrl master   ping  warnings");
    for (const Person& p : session.People_cai())
        Print_cai(PersonLine_cai(session, p));
    for (const Question& q : session.Questions_cai())
        Print_cai(fmt::format("  ? #{} {} {}  -> Session menu, 4 (answer)", q.fromId, q.fromName,
                              q.kind == Question::Kind::Request ? "asks you for control" : "offers you control"));
}

// The prefix of an event line.
const char* EventTag_cai(SessionEvent::Kind kind)
{
    switch (kind) {
        case SessionEvent::Kind::State:          return "state";
        case SessionEvent::Kind::People:         return "people";
        case SessionEvent::Kind::Question:       return "QUESTION";
        case SessionEvent::Kind::ControlChanged: return "control";
        case SessionEvent::Kind::Declined:       return "declined";
        case SessionEvent::Kind::Notice:         return "notice";
        case SessionEvent::Kind::Warning:        return "WARNING";
    }
    return "?";
}

// ======================================================================================
//  The interactive tool
// ======================================================================================

// ======================================================================================
//  sc_net_test.ini - what was typed last time (simple key=value lines, next to the tool)
// ======================================================================================

class ToolConfig {
public:
    // Reads `file` (missing = empty). Lines: key=value, '#' or ';' starts a comment.
    void Load_cai(const fs::path& file)
    {
        mFile = file;
        mValues.clear();
        std::error_code ec;
        if (!fs::exists(file, ec)) {
            LOG_INFO("no {} yet: it is written after the first change", Utils::Utf8_cai(file.filename()));
            return;
        }
        for (const std::string& raw : Utils::Split_cai(Utils::ReadAll_cai(file), '\n')) {
            const std::string_view line = Utils::Trim_cai(raw);
            const std::size_t equal = line.find('=');
            if (line.empty() || line[0] == '#' || line[0] == ';' || equal == std::string_view::npos)
                continue;
            mValues[std::string(Utils::Trim_cai(line.substr(0, equal)))] = std::string(Utils::Trim_cai(line.substr(equal + 1)));
        }
        LOG_INFO("settings read from {} ({} values)", Utils::Utf8_cai(file), mValues.size());
    }
    // Writes the file (only when something changed).
    void Save_cai()
    {
        std::string text = "# sc_net_test settings - written by the tool, safe to edit. Passwords are never saved.\n";
        for (const auto& [key, value] : mValues)
            text += fmt::format("{}={}\n", key, value);
        if (text == mWritten || mFile.empty())
            return;
        std::string error;
        if (Utils::WriteFileSafely_cai(mFile, text, error)) {
            mWritten = text;
            LOG_DEBUG("settings saved to {}", Utils::Utf8_cai(mFile));
        } else {
            LOG_WARN("cannot save {}: {}", Utils::Utf8_cai(mFile), error);
        }
    }
    // A value, or `fallback`.
    std::string Get_cai(const std::string& key, const std::string& fallback) const
    {
        const auto found = mValues.find(key);
        return found == mValues.end() || found->second.empty() ? fallback : found->second;
    }
    // A port value, or `fallback`.
    std::uint16_t Port_cai(const std::string& key, std::uint16_t fallback) const
    {
        int value = 0;
        return Utils::ParseInt_cai(Get_cai(key, ""), value) && value > 0 && value < 65536 ? static_cast<std::uint16_t>(value) : fallback;
    }
    // Sets a value (empty = removed).
    void Set_cai(const std::string& key, const std::string& value)
    {
        if (value.empty())
            mValues.erase(key);
        else
            mValues[key] = value;
    }
    void Clear_cai() { mValues.clear(); }
    const fs::path& File_cai() const { return mFile; }

private:
    fs::path mFile;
    std::map<std::string, std::string> mValues;
    std::string mWritten;
};

ToolConfig gConfig;

// The folder of the running tool (for sc_net_test.ini); the current folder when unknown.
fs::path ToolFolder_cai(const char* argv0)
{
    std::error_code ec;
#ifdef __linux__
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec && !self.empty())
        return self.parent_path();
#endif
    const fs::path given = argv0 ? fs::path(argv0) : fs::path();
    if (given.has_parent_path()) {
        const fs::path absolute = fs::absolute(given, ec);
        if (!ec)
            return absolute.parent_path();
    }
    return fs::current_path(ec);
}

class Console {
public:
    Console(LocalInfo local, HostSettings host, JoinSettings join) : mHost(host), mJoin(join)
    {
        mSession.SetLocal_cai(std::move(local));
        // The command names in the log, like the plugin.
        auto lmbda_namer = [this](std::uint16_t index) {
            const auto& list = Commands_cai();
            return index < list.size() ? fmt::format("{} ({})", list[index].label, list[index].name) : std::string();
        };
        mSession.SetCommandNamer_cai(lmbda_namer);
    }

    // Where the identity came from (saved in sc_net_test.ini): an aircraft path or a profile file.
    void SetSources_cai(std::string aircraftPath, std::string profileFile)
    {
        mAircraftPath = std::move(aircraftPath);
        mProfileFile = std::move(profileFile);
    }

    // Writes what is typed now to sc_net_test.ini (passwords never).
    void SaveConfig_cai()
    {
        const LocalInfo& me = mSession.Local_cai();
        gConfig.Set_cai("name", me.name);
        gConfig.Set_cai("aircraft", mAircraftPath);
        gConfig.Set_cai("profile", mAircraftPath.empty() ? mProfileFile : std::string());
        // .acf name and stamp only matter without an aircraft (it gives both).
        gConfig.Set_cai("acf", mAircraftPath.empty() ? me.acfName : std::string());
        gConfig.Set_cai("stamp", mAircraftPath.empty() ? me.acfStamp : std::string());
        gConfig.Set_cai("join_address", mJoin.address);
        gConfig.Set_cai("join_port", std::to_string(mJoin.port));
        gConfig.Set_cai("host_port", std::to_string(mHost.port));
        gConfig.Set_cai("max_people", std::to_string(mHost.maxPeople));
        gConfig.Set_cai("joiners_start_as", mHost.joinersStartAs == Role::Observer ? "observer" : "crew");
        gConfig.Set_cai("log_level", log::IsEnabled_cai(log::Level::Debug) ? "debug" : "info");
        gConfig.Set_cai("console_log", gConsoleLog.load() ? "on" : "off");
        gConfig.Save_cai();
    }

    // Starts hosting at once (--host).
    void StartHost_cai() { DoHost_cai(); }
    // Joins at once (--join).
    void StartJoin_cai() { DoJoin_cai(); }

    // Runs the menu until Quit; polls the session every 10 ms.
    int Run_cai()
    {
        auto lmbda_reader = [this]() {
            std::string line;
            while (std::getline(std::cin, line)) {
                std::lock_guard lock(mLinesMutex);
                mLines.push_back(line);
            }
            std::lock_guard lock(mLinesMutex);
            mLines.push_back("\x04");           // end of input = quit
        };
        std::thread(lmbda_reader).detach();
        SaveConfig_cai();             // the command-line values too
        ShowMenu_cai();
        while (!mQuit) {
            mSession.Poll_cai();
            PumpSharing_cai();
            for (const SessionEvent& event : mSession.TakeEvents_cai()) {
                if (event.kind == SessionEvent::Kind::People && mMenu != Menu::Session)
                    continue;
                // The session ended by itself (kicked, host left, refused, lost): back to the
                // menu it was started from, with the reason, instead of a dead Session menu.
                const SessionState state = mSession.State_cai();
                if (event.kind == SessionEvent::Kind::State &&
                    (state == SessionState::Failed || state == SessionState::Lost) && mMenu == Menu::Session) {
                    Print_cai(fmt::format("  * {}: {}", state == SessionState::Lost ? "session ended" : "could not connect",
                                          event.text));
                    mAnswer = nullptr;                    // a question being typed is no longer valid
                    mMenu = mStartedFrom;
                    ShowMenu_cai();
                    continue;
                }
                Print_cai(fmt::format("  * {}: {}", EventTag_cai(event.kind), event.text.empty() ? "changed" : event.text));
                if (event.kind == SessionEvent::Kind::People || event.kind == SessionEvent::Kind::Question)
                    PrintSession_cai(mSession);
            }
            std::vector<std::string> lines;
            {
                std::lock_guard lock(mLinesMutex);
                lines.assign(mLines.begin(), mLines.end());
                mLines.clear();
            }
            for (const std::string& line : lines) {
                if (line == "\x04") {
                    if (mWaitAfterEof && mSession.State_cai() != SessionState::Idle) {
                        mWaitAfterEof = false;          // input ended (a script): keep the session for --linger
                        mEofAt = std::chrono::steady_clock::now();
                        continue;
                    }
                    mQuit = true;
                    break;
                }
                HandleLine_cai(std::string(Utils::Trim_cai(line)));
                SaveConfig_cai();
                if (mQuit)
                    break;
            }
            if (mEofAt && std::chrono::steady_clock::now() - *mEofAt > std::chrono::seconds(mLingerSeconds))
                mQuit = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (mSession.State_cai() != SessionState::Idle)
            mSession.Leave_cai();
        Print_cai("bye");
        return 0;
    }

    // Keeps running `seconds` after the input ended (scripts piped into the tool).
    void SetLinger_cai(int seconds)
    {
        mWaitAfterEof = seconds > 0;
        mLingerSeconds = seconds;
    }

private:
    enum class Menu { Main, Identity, Host, Join, Session, Log, Sharing };
    using Answer = std::function<void(const std::string&)>;

    // Asks for a value; the next line goes to `answer` (empty line = keep `current`).
    void Ask_cai(const std::string& question, const std::string& current, Answer answer)
    {
        mAnswer = std::move(answer);
        mAnswerDefault = current;
        Print_cai(current.empty() ? fmt::format("  {}:", question) : fmt::format("  {} [{}]:", question, current));
    }

    // One line typed by the user.
    void HandleLine_cai(const std::string& line)
    {
        if (mAnswer) {
            Answer answer = std::move(mAnswer);
            mAnswer = nullptr;
            answer(line.empty() ? mAnswerDefault : line);
            if (!mAnswer)
                ShowMenu_cai();
            return;
        }
        if (line.empty()) {
            ShowMenu_cai();
            return;
        }
        int choice = -1;
        if (!Utils::ParseInt_cai(line, choice)) {
            Print_cai(fmt::format("  '{}' is not a menu number", line));
            return;
        }
        switch (mMenu) {
            case Menu::Main:     MainChoice_cai(choice); break;
            case Menu::Identity: IdentityChoice_cai(choice); break;
            case Menu::Host:     HostChoice_cai(choice); break;
            case Menu::Join:     JoinChoice_cai(choice); break;
            case Menu::Session:  SessionChoice_cai(choice); break;
            case Menu::Log:      LogChoice_cai(choice); break;
            case Menu::Sharing:  SharingChoice_cai(choice); break;
        }
        if (!mAnswer && !mQuit)
            ShowMenu_cai();
    }

    // Prints the current menu.
    void ShowMenu_cai()
    {
        const LocalInfo& me = mSession.Local_cai();
        switch (mMenu) {
            case Menu::Main:
                Print_cai(fmt::format("\n=== Shared Cockpit network test {} === you: '{}', {} - state {}", kToolVersion, me.name,
                                      me.acfName, StateName_cai(mSession.State_cai())));
                Print_cai("  1 Identity (name, aircraft, profile)");
                Print_cai("  2 Host a session");
                Print_cai("  3 Join a session");
                Print_cai("  4 Session (people, control, seats, notices, leave)");
                Print_cai("  5 Log level");
                Print_cai("  6 Sharing (shared commands and values: click, hold, set, random)");
                Print_cai("  0 Quit");
                break;
            case Menu::Identity:
                Print_cai("\n--- Identity ---");
                Print_cai(fmt::format("  (remembered in {})", Utils::Utf8_cai(gConfig.File_cai())));
                Print_cai(fmt::format("  1 Name             {}", me.name));
                Print_cai(fmt::format("  2 Aircraft (.acf)  {}", me.acfName));
                Print_cai(fmt::format("  3 .acf stamp       {}", me.acfStamp));
                Print_cai(fmt::format("  4 Load a profile (*.shared_cockpit.json)   now: {} ({} controls)",
                                      me.profileHash.empty() ? "none" : me.profileHash, me.allKeys.size()));
                Print_cai("  5 Use the demo profile");
                Print_cai("  6 Use the demo profile, variant (shares one control less)");
                Print_cai("  7 Use an aircraft (.acf file or its folder: reads its *.shared_cockpit.json, like the plugin)");
                Print_cai("  0 Back");
                break;
            case Menu::Host:
                Print_cai("\n--- Host a session ---");
                Print_cai(fmt::format("  1 Port             {}", mHost.port));
                Print_cai(fmt::format("  2 Password         {}", mHost.password.empty() ? "(none)" : mHost.password));
                Print_cai(fmt::format("  3 Max people       {} (you included)", mHost.maxPeople));
                Print_cai(fmt::format("  4 Joiners start as {}", mHost.joinersStartAs == Role::Crew
                                                                       ? "Crew (automatic)"
                                                                       : "Observer (I assign seats)"));
                Print_cai("  5 Start hosting");
                Print_cai("  0 Back");
                break;
            case Menu::Join:
                Print_cai("\n--- Join a session ---");
                Print_cai(fmt::format("  1 Host address     {}", mJoin.address.empty() ? "(not set)" : mJoin.address));
                Print_cai(fmt::format("  2 Port             {}", mJoin.port));
                Print_cai(fmt::format("  3 Password         {}", mJoin.password.empty() ? "(none)" : mJoin.password));
                Print_cai("  4 Join now");
                Print_cai("  0 Back");
                break;
            case Menu::Session:
                Print_cai("\n--- Session ---");
                PrintSession_cai(mSession);
                if (mSession.IsHost_cai() && mSession.State_cai() == SessionState::Hosting)
                    Print_cai(fmt::format("  next joiners start as {}  (change: Host menu, 4)",
                                          RoleName_cai(mSession.HostSettings_cai().joinersStartAs)));
                Print_cai("  1 Show people         2 Ask for control      3 Offer control");
                Print_cai("  4 Answer a question   5 Take control (master)");
                Print_cai("  6 Set a seat (master) 7 Send a notice        8 Remove someone (master)");
                Print_cai(fmt::format("  9 {}            0 Back", mSession.IsHost_cai() ? "End session" : "Leave      "));
                break;
            case Menu::Sharing: {
                const auto& list = Commands_cai();
                Print_cai(fmt::format("\n--- Sharing --- {} shared commands, {} shared values, state {}", list.size(),
                                      Datarefs_cai().size(), StateName_cai(mSession.State_cai())));
                Print_cai(fmt::format("  commands: sent {}, received {}, not shared {}", mSent, mReceived, mRejected));
                Print_cai(fmt::format("  values:   sent {}, received {}, snapped back {}{}", mValuesSent, mValuesReceived, mCorrections,
                                      mRandomUntil ? (mRandomValues ? "   (random values running)" : "   (random clicks running)") : ""));
                Print_cai("  1 List the commands (index, name)");
                Print_cai("  2 Click a command (Begin + End)       3 Hold a command (Begin)   4 Release it (End)");
                Print_cai("  5 Random clicks (every N ms, for S seconds)");
                Print_cai(fmt::format("  6 Show received commands / values: {}", mShowReceived ? "on" : "off"));
                Print_cai("  7 List the values (index, value, dataref)");
                Print_cai("  8 Set a value                         9 Random value changes (every N ms, for S seconds)");
                Print_cai(fmt::format("  10 Set my position (sent every second){}   11 Show positions / distance to the PIC",
                                      mMyPosition ? fmt::format(" [{:.5f},{:.5f} {:.0f}°]", mMyPosition->latitude, mMyPosition->longitude,
                                                                mMyPosition->heading) : std::string()));
                Print_cai("  0 Back");
                break;
            }
            case Menu::Log:
                Print_cai("\n--- Log ---");
                Print_cai(fmt::format("  file: {}", Utils::Utf8_cai(log::GetLogFilePath_cai())));
                Print_cai(fmt::format("  1 Info (states, handshake, seats, control)   {}", log::IsEnabled_cai(log::Level::Debug) ? "" : "<- now"));
                Print_cai(fmt::format("  2 Debug (also every message and packet)      {}", log::IsEnabled_cai(log::Level::Debug) ? "<- now" : ""));
                Print_cai(fmt::format("  3 Log on the console: {}", gConsoleLog.load() ? "on" : "off"));
                Print_cai("  0 Back");
                break;
        }
        Print_cai("> ");
    }

    // Main menu.
    void MainChoice_cai(int choice)
    {
        switch (choice) {
            case 1: mMenu = Menu::Identity; break;
            case 2: mMenu = Menu::Host; break;
            case 3: mMenu = Menu::Join; break;
            case 4: mMenu = Menu::Session; break;
            case 5: mMenu = Menu::Log; break;
            case 6: mMenu = Menu::Sharing; break;
            case 0: mQuit = true; break;
            default: Print_cai("  no such choice");
        }
    }

    // Identity menu.
    void IdentityChoice_cai(int choice)
    {
        auto lmbda_apply = [this](LocalInfo info) {
            if (mSession.State_cai() != SessionState::Idle)
                Print_cai("  (used from the next host / join)");
            mSession.SetLocal_cai(std::move(info));
        };
        const LocalInfo local = mSession.Local_cai();
        auto lmbda_setName = [this, lmbda_apply](const std::string& v) { LocalInfo l = mSession.Local_cai(); l.name = v; lmbda_apply(l); };
        auto lmbda_setAcf = [this, lmbda_apply](const std::string& v) { LocalInfo l = mSession.Local_cai(); l.acfName = v; lmbda_apply(l); };
        auto lmbda_setStamp = [this, lmbda_apply](const std::string& v) { LocalInfo l = mSession.Local_cai(); l.acfStamp = v; lmbda_apply(l); };
        auto lmbda_loadProfile = [this, lmbda_apply](const std::string& v) {
            LocalInfo l = mSession.Local_cai();
            std::string error;
            if (LoadProfile_cai(l, v, error)) {
                lmbda_apply(l);
                SetSources_cai({}, v);
                Print_cai("  profile loaded");
            } else {
                Print_cai(fmt::format("  cannot use it: {}", error));
            }
        };
        auto lmbda_loadAircraft = [this, lmbda_apply](const std::string& v) {
            LocalInfo l = mSession.Local_cai();
            std::string error;
            if (LoadAircraft_cai(l, v, error)) {
                lmbda_apply(l);
                SetSources_cai(v, {});
                Print_cai(fmt::format("  aircraft {} (stamp {}), profile {}", l.acfName, l.acfStamp,
                                      l.profileHash.empty() ? "none" : l.profileHash));
            } else {
                Print_cai(fmt::format("  cannot use it: {}", error));
            }
        };
        switch (choice) {
            case 1: Ask_cai("Your name", local.name, lmbda_setName); break;
            case 7: Ask_cai("Path of the .acf file or the aircraft folder", "", lmbda_loadAircraft); break;
            case 2: Ask_cai("Aircraft .acf file name", local.acfName, lmbda_setAcf); break;
            case 3: Ask_cai(".acf stamp (any text; differs = 'other version')", local.acfStamp, lmbda_setStamp); break;
            case 4: Ask_cai("Path of a *.shared_cockpit.json file", "", lmbda_loadProfile); break;
            case 5:
            case 6: {
                LocalInfo l = local;
                UseAnalysis_cai(l, DemoAnalysis_cai(l.acfName, l.acfStamp, choice == 6 ? 1 : 0));
                lmbda_apply(l);
                SetSources_cai({}, {});
                Print_cai(fmt::format("  demo profile {}", l.profileHash));
                break;
            }
            case 0: mMenu = Menu::Main; break;
            default: Print_cai("  no such choice");
        }
    }

    // A port from text (false = not a port).
    static bool ParsePort_cai(const std::string& text, std::uint16_t& port)
    {
        int value = 0;
        if (!Utils::ParseInt_cai(text, value) || value <= 0 || value >= 65536) {
            Print_cai("  not a port (1 to 65535)");
            return false;
        }
        port = static_cast<std::uint16_t>(value);
        return true;
    }

    // Host menu.
    void HostChoice_cai(int choice)
    {
        auto lmbda_setPort = [this](const std::string& v) { ParsePort_cai(v, mHost.port); };
        auto lmbda_setPassword = [this](const std::string& v) { mHost.password = v == "-" ? "" : v; };
        auto lmbda_setMax = [this](const std::string& v) {
            int max = 0;
            if (Utils::ParseInt_cai(v, max) && max >= 2 && max <= kMaxPeople)
                mHost.maxPeople = max;
            else
                Print_cai("  2, 3 or 4");
        };
        switch (choice) {
            case 1: Ask_cai("UDP port", std::to_string(mHost.port), lmbda_setPort); break;
            case 2: Ask_cai("Password ('-' = none)", mHost.password, lmbda_setPassword); break;
            case 3: Ask_cai("Max people, 2 to 4", std::to_string(mHost.maxPeople), lmbda_setMax); break;
            case 4: {
                mHost.joinersStartAs = mHost.joinersStartAs == Role::Crew ? Role::Observer : Role::Crew;
                if (mSession.State_cai() == SessionState::Hosting) {      // hosting: applies to the next joiners
                    std::string why;
                    Report_cai(mSession.SetJoinersStartAs_cai(mHost.joinersStartAs, why), why);
                }
                break;
            }
            case 5: DoHost_cai(); break;
            case 0: mMenu = Menu::Main; break;
            default: Print_cai("  no such choice");
        }
    }

    // Join menu.
    void JoinChoice_cai(int choice)
    {
        auto lmbda_setAddress = [this](const std::string& v) { SetAddress_cai(v); };
        auto lmbda_setPort = [this](const std::string& v) { ParsePort_cai(v, mJoin.port); };
        auto lmbda_setPassword = [this](const std::string& v) { mJoin.password = v == "-" ? "" : v; };
        switch (choice) {
            case 1: Ask_cai("Host address (IP or name; address:port also works)", mJoin.address, lmbda_setAddress); break;
            case 2: Ask_cai("UDP port", std::to_string(mJoin.port), lmbda_setPort); break;
            case 3: Ask_cai("Password ('-' = none)", mJoin.password, lmbda_setPassword); break;
            case 4: DoJoin_cai(); break;
            case 0: mMenu = Menu::Main; break;
            default: Print_cai("  no such choice");
        }
    }

    // An id from text (false = not an id).
    static bool ParseId_cai(const std::string& text, std::uint8_t& id)
    {
        int value = -1;
        if (!Utils::ParseInt_cai(text, value) || value < 0 || value > 250) {
            Print_cai("  not an id (see the 'id' column)");
            return false;
        }
        id = static_cast<std::uint8_t>(value);
        return true;
    }

    // Prints why an action did not work.
    static void Report_cai(bool ok, const std::string& why)
    {
        if (!ok)
            Print_cai(fmt::format("  cannot: {}", why));
    }

    // Session menu.
    void SessionChoice_cai(int choice)
    {
        std::string why;
        auto lmbda_offer = [this](const std::string& v) {
            std::uint8_t id = kNoId;
            std::string reason;
            if (ParseId_cai(v, id))
                Report_cai(mSession.OfferControl_cai(id, reason), reason);
        };
        auto lmbda_seatFor = [this](const std::string& v) {
            std::uint8_t id = kNoId;
            if (!ParseId_cai(v, id))
                return;
            auto lmbda_seat = [this, id](const std::string& seat) {
                std::string reason;
                const Role role = !seat.empty() && (seat[0] == 'o' || seat[0] == 'O') ? Role::Observer : Role::Crew;
                Report_cai(mSession.SetRole_cai(id, role, reason), reason);
            };
            Ask_cai("c = Crew, o = Observer", "c", lmbda_seat);
        };
        auto lmbda_notice = [this](const std::string& v) {
            std::string reason;
            Report_cai(mSession.SendNotice_cai(v, reason), reason);
        };
        auto lmbda_kick = [this](const std::string& v) {
            std::uint8_t id = kNoId;
            std::string reason;
            if (ParseId_cai(v, id))
                Report_cai(mSession.Kick_cai(id, reason), reason);
        };
        switch (choice) {
            case 1: break;    // the menu prints the people
            case 2: Report_cai(mSession.RequestControl_cai(why), why); break;
            case 3: Ask_cai("Offer control to id", "", lmbda_offer); break;
            case 4: {
                if (mSession.Questions_cai().empty()) {
                    Print_cai("  no open question");
                    break;
                }
                const Question q = mSession.Questions_cai().front();
                auto lmbda_answer = [this, q](const std::string& v) {
                    std::string reason;
                    const bool yes = !v.empty() && (v[0] == 'y' || v[0] == 'Y');
                    Report_cai(mSession.AnswerControl_cai(q.fromId, yes, reason), reason);
                };
                Ask_cai(fmt::format("{} {} - yes or no (y/n)", q.fromName,
                                    q.kind == Question::Kind::Request ? "asks you for control" : "offers you control"),
                        "", lmbda_answer);
                break;
            }
            case 5: Report_cai(mSession.TakeControl_cai(why), why); break;
            case 6: Ask_cai("Seat for id", "", lmbda_seatFor); break;
            case 7: Ask_cai("Message", "", lmbda_notice); break;
            case 8: Ask_cai("Remove id", "", lmbda_kick); break;
            case 9: mSession.Leave_cai(); break;
            case 0: mMenu = Menu::Main; break;
            default: Print_cai("  no such choice");
        }
    }

    // The session's shared commands (the host's analysis when it was sent, else ours).
    const std::vector<analysis::SharedCommand>& Commands_cai()
    {
        const std::string source = mSession.HostProfile_cai() ? "host:" + mSession.HostProfile_cai()->acfStamp + std::to_string(mSession.HostProfile_cai()->controls.size())
                                                              : mSession.Local_cai().profileHash;
        if (source != mCommandsSource) {
            mCommandsSource = source;
            mCommands.clear();
            mDatarefs.clear();
            mValues.clear();
            if (mSession.HostProfile_cai()) {
                mCommands = analysis::SharedCommands_cai(*mSession.HostProfile_cai());
                mDatarefs = analysis::SharedDatarefs_cai(*mSession.HostProfile_cai());
            } else if (!mSession.Local_cai().profileText.empty()) {
                analysis::AnalysisData data;
                std::string error;
                if (store::MakeAnalysisStore_cai()->FromText_cai(mSession.Local_cai().profileText, data, error)) {
                    mCommands = analysis::SharedCommands_cai(data);
                    mDatarefs = analysis::SharedDatarefs_cai(data);
                }
            }
        }
        return mCommands;
    }

    // The session's shared datarefs (built with the commands).
    const std::vector<analysis::SharedDataref>& Datarefs_cai()
    {
        Commands_cai();
        return mDatarefs;
    }

    // "v3 FUEL PUMP ON [1] (sim/cockpit2/switches/fuel_pump_on[1])".
    std::string DatarefText_cai(std::uint16_t index)
    {
        const auto& list = Datarefs_cai();
        return index < list.size() ? fmt::format("v{} {} ({})", index, list[index].label, list[index].ref)
                                   : fmt::format("v{} (not in the list)", index);
    }

    // The value of one dataref here (0 when never set).
    double ValueOf_cai(std::uint16_t index) const
    {
        const auto at = mValues.find(index);
        return at == mValues.end() ? 0.0 : at->second;
    }

    // Sets a value here and sends it.
    void SendValue_cai(std::uint16_t index, double value)
    {
        std::string why;
        if (mSession.SendValues_cai({{index, value}}, why)) {
            mValues[index] = value;
            ++mValuesSent;
            LOG_INFO("sent {} = {}", DatarefText_cai(index), value);
        } else {
            Print_cai(fmt::format("  cannot: {}", why));
        }
    }

    // "#12 BATTERY 1 ON (sim/electrical/battery_1_on)".
    std::string CommandText_cai(std::uint16_t index)
    {
        const auto& list = Commands_cai();
        return index < list.size() ? fmt::format("#{} {} ({})", index, list[index].label, list[index].name)
                                   : fmt::format("#{} (not in the list)", index);
    }

    // Sends one phase of a command.
    void SendCommand_cai(std::uint16_t index, CommandPhase phase)
    {
        std::string why;
        if (mSession.SendCommand_cai(index, phase, why)) {
            ++mSent;
            LOG_INFO("sent {} {}", phase == CommandPhase::Begin ? "Begin" : "End", CommandText_cai(index));
        } else {
            Print_cai(fmt::format("  cannot: {}", why));
        }
    }

    // Prints / counts the commands from the others and the rejections; runs the random clicks.
    void PumpSharing_cai()
    {
        for (const RemoteCommand& c : mSession.TakeCommands_cai()) {
            ++mReceived;
            if (mShowReceived)
                Print_cai(fmt::format("  * command: {} {} {}", mSession.NameOf_cai(c.author),
                                      c.phase == CommandPhase::Begin ? "BEGIN" : "END  ", CommandText_cai(c.index)));
        }
        for (const Rejection& r : mSession.TakeRejections_cai()) {
            ++mRejected;
            Print_cai(fmt::format("  * not shared: {} - {}", CommandText_cai(r.index), r.text));
        }
        for (const RemoteValues& rv : mSession.TakeValues_cai()) {
            for (const DataValue& v : rv.values)
                mValues[v.index] = v.value;
            switch (rv.kind) {
                case RemoteValues::Kind::Change:
                    mValuesReceived += rv.values.size();
                    if (mShowReceived)
                        for (const DataValue& v : rv.values)
                            Print_cai(fmt::format("  * value: {} {} = {}", mSession.NameOf_cai(rv.author), DatarefText_cai(v.index), v.value));
                    break;
                case RemoteValues::Kind::Correction:
                    mCorrections += rv.values.size();
                    for (const DataValue& v : rv.values)
                        Print_cai(fmt::format("  * snapped back: {} = {} - {}", DatarefText_cai(v.index), v.value, rv.text));
                    break;
                case RemoteValues::Kind::Snapshot:
                    Print_cai(fmt::format("  * snapshot from the host: {} values", rv.values.size()));
                    break;
            }
        }
        if (const auto weather = mSession.TakeWeather_cai(); weather && !mWeatherSeen) {
            mWeatherSeen = true;
            Print_cai(fmt::format("  * weather from the host: {} values (shown once)", weather->size()));
        }
        if (mMyPosition && mSession.Active_cai() && std::chrono::steady_clock::now() >= mNextPosition) {
            mNextPosition = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            mSession.SendPosition_cai(*mMyPosition);
        }
        if (mSession.IsHost_cai()) {
            for (std::uint8_t id : mSession.TakeSnapshotRequests_cai()) {
                std::vector<DataValue> values;
                for (std::size_t i = 0; i < Datarefs_cai().size(); ++i)
                    values.push_back({static_cast<std::uint16_t>(i), ValueOf_cai(static_cast<std::uint16_t>(i))});
                mSession.SendSnapshot_cai(id, values);
                Print_cai(fmt::format("  * snapshot sent to {}: {} values", mSession.NameOf_cai(id), values.size()));
            }
        }
        if (!mRandomUntil)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (now >= *mRandomUntil || !mSession.Active_cai()) {
            mRandomUntil.reset();
            Print_cai(fmt::format("  random clicks done ({} sent so far)", mSent));
            return;
        }
        if (mRandomValues && now >= mNextRandom && !Datarefs_cai().empty()) {
            mNextRandom = now + std::chrono::milliseconds(mRandomEveryMs);
            const auto index = static_cast<std::uint16_t>(std::rand() % static_cast<int>(Datarefs_cai().size()));
            SendValue_cai(index, static_cast<double>(std::rand() % 101) / 100.0);   // 0.00 .. 1.00
        } else if (!mRandomValues && now >= mNextRandom && !Commands_cai().empty()) {
            mNextRandom = now + std::chrono::milliseconds(mRandomEveryMs);
            const auto index = static_cast<std::uint16_t>(std::rand() % static_cast<int>(Commands_cai().size()));
            SendCommand_cai(index, CommandPhase::Begin);
            SendCommand_cai(index, CommandPhase::End);
        }
    }

    // Sharing menu.
    void SharingChoice_cai(int choice)
    {
        auto lmbda_index = [this](const std::string& v, std::uint16_t& index) {
            int value = -1;
            if (!Utils::ParseInt_cai(v, value) || value < 0 || value >= static_cast<int>(Commands_cai().size())) {
                Print_cai(fmt::format("  0 to {}", static_cast<int>(Commands_cai().size()) - 1));
                return false;
            }
            index = static_cast<std::uint16_t>(value);
            return true;
        };
        auto lmbda_click = [this, lmbda_index](const std::string& v) {
            std::uint16_t index = 0;
            if (lmbda_index(v, index)) {
                SendCommand_cai(index, CommandPhase::Begin);
                SendCommand_cai(index, CommandPhase::End);
            }
        };
        auto lmbda_hold = [this, lmbda_index](const std::string& v) {
            std::uint16_t index = 0;
            if (lmbda_index(v, index)) {
                SendCommand_cai(index, CommandPhase::Begin);
                mHeld = index;
            }
        };
        auto lmbda_release = [this, lmbda_index](const std::string& v) {
            std::uint16_t index = 0;
            if (lmbda_index(v, index))
                SendCommand_cai(index, CommandPhase::End);
        };
        auto lmbda_randomFor = [this](const std::string& v) {
            int seconds = 0;
            if (!Utils::ParseInt_cai(v, seconds) || seconds <= 0) {
                Print_cai("  a number of seconds");
                return;
            }
            mRandomUntil = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
            mNextRandom = std::chrono::steady_clock::now();
            Print_cai(fmt::format("  random {} every {} ms for {} s", mRandomValues ? "value changes" : "clicks", mRandomEveryMs, seconds));
        };
        auto lmbda_randomEvery = [this, lmbda_randomFor](const std::string& v) {
            int ms = 0;
            if (!Utils::ParseInt_cai(v, ms) || ms < 20) {
                Print_cai("  at least 20 ms");
                return;
            }
            mRandomEveryMs = ms;
            Ask_cai("For how many seconds", "10", lmbda_randomFor);
        };
        auto lmbda_valueIndex = [this](const std::string& v, std::uint16_t& index) {
            int value = -1;
            if (!Utils::ParseInt_cai(v, value) || value < 0 || value >= static_cast<int>(Datarefs_cai().size())) {
                Print_cai(fmt::format("  0 to {}", static_cast<int>(Datarefs_cai().size()) - 1));
                return false;
            }
            index = static_cast<std::uint16_t>(value);
            return true;
        };
        auto lmbda_setValue = [this](const std::string& v) {
            double value = 0.0;
            try {
                std::size_t used = 0;
                value = std::stod(v, &used);
                if (used != v.size())
                    throw std::invalid_argument(v);
            } catch (const std::exception&) {
                Print_cai("  a number, e.g. 1 or 0.75");
                return;
            }
            SendValue_cai(mValueIndex, value);
        };
        auto lmbda_setIndex = [this, lmbda_valueIndex, lmbda_setValue](const std::string& v) {
            if (!lmbda_valueIndex(v, mValueIndex))
                return;
            const double now = ValueOf_cai(mValueIndex);
            Ask_cai(fmt::format("New value for {} (now {})", DatarefText_cai(mValueIndex), now),
                    fmt::format("{}", now == 0.0 ? 1.0 : 0.0), lmbda_setValue);
        };
        auto lmbda_position = [this](const std::string& v) {
            const std::vector<std::string> parts = Utils::Split_cai(v, ',');
            PositionInfo p;
            try {
                if (parts.size() < 3)
                    throw std::invalid_argument(v);
                p.latitude = std::stod(parts[0]);
                p.longitude = std::stod(parts[1]);
                p.heading = std::stof(parts[2]);
            } catch (const std::exception&) {
                Print_cai("  e.g. 32.0114,34.8867,120   (add ,air and / or ,engines)");
                return;
            }
            for (std::size_t i = 3; i < parts.size(); ++i) {
                const std::string word = Utils::Lower_cai(Utils::Trim_cai(parts[i]));
                if (word == "air") { p.onGround = false; p.elevation = 1500.0; p.groundSpeed = 60.0f; }
                if (word == "engines") { p.engines = 1; }
            }
            p.engineCount = 1;
            mMyPosition = p;
            mNextPosition = {};
            Print_cai(fmt::format("  sending {:.6f},{:.6f} heading {:.0f} {} every second", p.latitude, p.longitude, p.heading,
                                  p.onGround ? "on the ground" : "in the air"));
        };
        switch (choice) {
            case 1: {
                const auto& list = Commands_cai();
                if (list.empty())
                    Print_cai("  no shared commands (no profile, or none ticked)");
                for (std::size_t i = 0; i < list.size(); ++i)
                    Print_cai(fmt::format("   {:>3}  {:<28} {}", i, list[i].label, list[i].name));
                break;
            }
            case 2: Ask_cai("Click command index", "", lmbda_click); break;
            case 3: Ask_cai("Hold command index", "", lmbda_hold); break;
            case 4: Ask_cai("Release command index", std::to_string(mHeld), lmbda_release); break;
            case 5: mRandomValues = false; Ask_cai("Every how many ms", std::to_string(mRandomEveryMs), lmbda_randomEvery); break;
            case 6: mShowReceived = !mShowReceived; break;
            case 7: {
                const auto& list = Datarefs_cai();
                if (list.empty())
                    Print_cai("  no shared values (no profile, or no dataref ticked)");
                for (std::size_t i = 0; i < list.size(); ++i)
                    Print_cai(fmt::format("   {:>3}  {:<28} = {:<8} {}", i, list[i].label,
                                          ValueOf_cai(static_cast<std::uint16_t>(i)), list[i].ref));
                break;
            }
            case 8: Ask_cai("Set value index", std::to_string(mValueIndex), lmbda_setIndex); break;
            case 10:
                Ask_cai("My position: latitude,longitude,heading[,air][,engines]", "32.0114,34.8867,120", lmbda_position);
                break;
            case 11: {
                const auto& known = mSession.Positions_cai();
                if (known.empty())
                    Print_cai("  no positions yet");
                const auto pic = known.find(mSession.PicId_cai());
                for (const auto& [id, at] : known) {
                    const PositionInfo& p = at.position;
                    const std::string gap = pic != known.end() && id != pic->first
                        ? fmt::format(", {} / {:.0f}° from the PIC", Utils::DistanceText_cai(Utils::DistanceM_cai(p.latitude, p.longitude,
                              pic->second.position.latitude, pic->second.position.longitude)),
                              Utils::HeadingDiff_cai(p.heading, pic->second.position.heading))
                        : std::string(id == mSession.PicId_cai() ? " (PIC)" : "");
                    Print_cai(fmt::format("   {} {:<12} {:.6f},{:.6f} hdg {:.0f} {} {} engines{}", id, mSession.NameOf_cai(id), p.latitude,
                                          p.longitude, p.heading, p.onGround ? "ground" : "air", p.engines, gap));
                }
                break;
            }
            case 9: mRandomValues = true; Ask_cai("Every how many ms", std::to_string(mRandomEveryMs), lmbda_randomEvery); break;
            case 0: mMenu = Menu::Main; break;
            default: Print_cai("  no such choice");
        }
    }

    // Log menu.
    void LogChoice_cai(int choice)
    {
        switch (choice) {
            case 1: log::SetLevel_cai(log::Level::Info); LOG_INFO("log level: Info"); break;
            case 2: log::SetLevel_cai(log::Level::Debug); LOG_INFO("log level: Debug"); break;
            case 3: gConsoleLog = !gConsoleLog.load(); break;
            case 0: mMenu = Menu::Main; break;
            default: Print_cai("  no such choice");
        }
    }

    // "host", "host:port" into the join settings.
    void SetAddress_cai(const std::string& text)
    {
        const std::size_t colon = text.rfind(':');
        int port = 0;
        if (colon != std::string::npos && text.find(':') == colon && Utils::ParseInt_cai(text.substr(colon + 1), port) &&
            port > 0 && port < 65536) {
            mJoin.address = text.substr(0, colon);
            mJoin.port = static_cast<std::uint16_t>(port);
        } else {
            mJoin.address = text;
        }
    }

    // Starts hosting with the menu's settings.
    void DoHost_cai()
    {
        mStartedFrom = Menu::Host;
        if (mSession.Host_cai(mHost)) {
            Print_cai(fmt::format("  hosting on UDP port {}. Others join with your address (LAN IP, forwarded port, ZeroTier "
                                  "or Tailscale IP) and port {}.", mHost.port, mHost.port));
            mMenu = Menu::Session;
        } else {
            Print_cai(fmt::format("  cannot host: {}", mSession.FailText_cai()));
        }
    }

    // Joins with the menu's settings.
    void DoJoin_cai()
    {
        mStartedFrom = Menu::Join;
        if (mSession.Join_cai(mJoin))
            mMenu = Menu::Session;
        else
            Print_cai(fmt::format("  cannot join: {}", mSession.FailText_cai()));
    }

    NetSession   mSession;
    HostSettings mHost;
    JoinSettings mJoin;
    Menu         mMenu = Menu::Main;
    Menu         mStartedFrom = Menu::Main;
    std::string  mAircraftPath;              // --aircraft / Identity 7 (saved)
    std::vector<analysis::SharedCommand> mCommands;
    std::vector<analysis::SharedDataref> mDatarefs;   // built with mCommands
    std::map<std::uint16_t, double> mValues;          // this "cockpit's" dataref values (0 when never set)
    std::string  mCommandsSource;
    bool         mRandomValues = false;
    std::optional<PositionInfo> mMyPosition;          // Sharing 10: sent every second
    std::chrono::steady_clock::time_point mNextPosition;
    bool         mWeatherSeen = false;
    std::uint16_t mValueIndex = 0;              // random mode 7: value changes instead of clicks
    std::uint64_t mValuesSent = 0, mValuesReceived = 0, mCorrections = 0;
    std::uint64_t mSent = 0, mReceived = 0, mRejected = 0;
    bool         mShowReceived = true;
    std::uint16_t mHeld = 0;
    int          mRandomEveryMs = 200;
    std::optional<std::chrono::steady_clock::time_point> mRandomUntil;
    std::chrono::steady_clock::time_point mNextRandom;
    std::string  mProfileFile;               // --profile / Identity 4 (saved)
    Answer       mAnswer;
    std::string  mAnswerDefault;
    bool         mQuit = false;
    bool         mWaitAfterEof = false;
    int          mLingerSeconds = 0;
    std::optional<std::chrono::steady_clock::time_point> mEofAt;
    std::mutex   mLinesMutex;
    std::deque<std::string> mLines;
};

// ======================================================================================
//  --selftest
// ======================================================================================

class SelfTest {
public:
    // Runs every check; the exit code is the number of failures.
    int Run_cai()
    {
        const std::uint16_t port = 39177;
        const std::string acf = "Cessna_172SP_G1000.acf";
        Print_cai(fmt::format("=== self test on 127.0.0.1:{} ===", port));

        NetSession host, ann, bob, cid;
        host.SetLocal_cai(DemoLocal_cai("Host", acf, "100;1700000000", 0));
        ann.SetLocal_cai(DemoLocal_cai("Ann", acf, "100;1700000000", 0));
        bob.SetLocal_cai(DemoLocal_cai("Bob", acf, "100;1700000099", 1));   // other stamp, other profile
        std::vector<NetSession*> all{&host, &ann, &bob, &cid};
        auto lmbda_pump = [&all](double seconds, const std::function<bool()>& done) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(seconds * 1000));
            while (std::chrono::steady_clock::now() < until) {
                for (NetSession* s : all)
                    s->Poll_cai();
                if (done && done())
                    return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return done ? done() : true;
        };
        auto lmbda_state = [](const NetSession& s, SessionState state) {
            auto lmbda_isState = [&s, state]() { return s.State_cai() == state; };
            return lmbda_isState;
        };
        auto lmbda_ended = [](const NetSession& s) {
            auto lmbda_isEnded = [&s]() { return s.State_cai() == SessionState::Failed || s.State_cai() == SessionState::Lost; };
            return lmbda_isEnded;
        };
        auto lmbda_pic = [](const NetSession& s, std::uint8_t id) {
            auto lmbda_isPic = [&s, id]() { return s.PicId_cai() == id; };
            return lmbda_isPic;
        };
        auto lmbda_hasQuestion = [](const NetSession& s) {
            auto lmbda_asked = [&s]() { return !s.Questions_cai().empty(); };
            return lmbda_asked;
        };
        auto lmbda_peopleCount = [](const NetSession& s, std::size_t count) {
            auto lmbda_counted = [&s, count]() { return s.People_cai().size() == count; };
            return lmbda_counted;
        };
        // True once `s` got an event whose text contains `word`.
        auto lmbda_heard = [](NetSession& s, std::string word, bool& flag) {
            auto lmbda_listen = [&s, word, &flag]() {
                for (const SessionEvent& e : s.TakeEvents_cai())
                    flag = flag || Utils::Contains_cai(e.text, word);
                return flag;
            };
            return lmbda_listen;
        };

        // 1. Host.
        HostSettings settings;
        settings.port = port;
        settings.password = "secret";
        settings.maxPeople = 3;
        Check_cai("host starts", host.Host_cai(settings) && lmbda_pump(0.3, nullptr) && host.State_cai() == SessionState::Hosting,
                  host.FailText_cai());

        JoinSettings join;
        join.address = "127.0.0.1";
        join.port = port;

        // 2. Wrong password.
        join.password = "wrong";
        ann.Join_cai(join);
        lmbda_pump(6, lmbda_ended(ann));
        Check_cai("wrong password is refused", ann.State_cai() == SessionState::Failed &&
                  Utils::Contains_cai(Utils::Lower_cai(ann.FailText_cai()), "password"), ann.FailText_cai());

        // 3. Another aircraft.
        cid.SetLocal_cai(DemoLocal_cai("Cid", "Baron_58.acf", "1", 0));
        join.password = "secret";
        cid.Join_cai(join);
        lmbda_pump(6, lmbda_ended(cid));
        Check_cai("another aircraft is refused", cid.State_cai() == SessionState::Failed &&
                  Utils::Contains_cai(cid.FailText_cai(), "Baron"), cid.FailText_cai());

        // 4. Ann joins (same profile), Bob joins (other profile, other stamp).
        ann.Join_cai(join);
        lmbda_pump(6, lmbda_state(ann, SessionState::InSession));
        Check_cai("Ann joins (same profile)", ann.State_cai() == SessionState::InSession && !ann.HostProfile_cai(),
                  ann.FailText_cai());

        // 5. Same name.
        cid.SetLocal_cai(DemoLocal_cai("ann", acf, "100;1700000000", 0));
        cid.Join_cai(join);
        lmbda_pump(6, lmbda_ended(cid));
        Check_cai("a name already in the session is refused", cid.State_cai() == SessionState::Failed &&
                  Utils::Contains_cai(cid.FailText_cai(), "already"), cid.FailText_cai());

        bob.Join_cai(join);
        lmbda_pump(6, lmbda_state(bob, SessionState::InSession));
        Check_cai("Bob joins and gets the host's profile", bob.State_cai() == SessionState::InSession && bob.HostProfile_cai().has_value(),
                  bob.FailText_cai());
        lmbda_pump(3, lmbda_peopleCount(ann, 3));
        lmbda_pump(3, lmbda_peopleCount(bob, 3));
        Check_cai("everybody sees 3 people", host.People_cai().size() == 3 && ann.People_cai().size() == 3 &&
                  bob.People_cai().size() == 3, fmt::format("{} {} {}", host.People_cai().size(), ann.People_cai().size(), bob.People_cai().size()));
        const Person* bobSeen = host.Find_cai(bob.MyId_cai());
        Check_cai("Bob is flagged: other .acf version + host profile",
                  bobSeen && (bobSeen->flags & kFlagAcfVersion) && (bobSeen->flags & kFlagHostProfile), "");

        // 6. Full (max 3).
        cid.SetLocal_cai(DemoLocal_cai("Cid", acf, "100;1700000000", 0));
        cid.Join_cai(join);
        lmbda_pump(6, lmbda_ended(cid));
        Check_cai("a 4th person is refused when max is 3", cid.State_cai() == SessionState::Failed &&
                  Utils::Contains_cai(cid.FailText_cai(), "full"), cid.FailText_cai());

        // 7. Ann asks for control, the host says yes.
        std::string why;
        Check_cai("Ann asks for control", ann.RequestControl_cai(why), why);
        lmbda_pump(3, lmbda_hasQuestion(host));
        Check_cai("the host gets the question", !host.Questions_cai().empty(), "");
        host.AnswerControl_cai(ann.MyId_cai(), true, why);
        lmbda_pump(3, lmbda_pic(ann, ann.MyId_cai()));
        lmbda_pump(3, lmbda_pic(bob, ann.MyId_cai()));
        Check_cai("Ann is PIC everywhere", host.PicId_cai() == ann.MyId_cai() && ann.PicId_cai() == ann.MyId_cai() &&
                  bob.PicId_cai() == ann.MyId_cai(), "");

        // 8. Ann offers to Bob, Bob says no, then yes.
        Check_cai("Ann offers control to Bob", ann.OfferControl_cai(bob.MyId_cai(), why), why);
        lmbda_pump(3, lmbda_hasQuestion(bob));
        bob.AnswerControl_cai(ann.MyId_cai(), false, why);
        bool declined = false;
        lmbda_pump(3, lmbda_heard(ann, "said no", declined));
        Check_cai("Ann hears that Bob said no", declined && host.PicId_cai() == ann.MyId_cai(), "");
        ann.OfferControl_cai(bob.MyId_cai(), why);
        lmbda_pump(3, lmbda_hasQuestion(bob));
        bob.AnswerControl_cai(ann.MyId_cai(), true, why);
        lmbda_pump(3, lmbda_pic(ann, bob.MyId_cai()));
        Check_cai("Bob accepts: Bob is PIC", host.PicId_cai() == bob.MyId_cai() && ann.PicId_cai() == bob.MyId_cai(), "");

        // 9. The master seats Bob as observer: control returns to the host.
        Check_cai("the master makes Bob an observer", host.SetRole_cai(bob.MyId_cai(), Role::Observer, why), why);
        lmbda_pump(3, lmbda_pic(bob, kHostId));
        Check_cai("control returns to the host", host.PicId_cai() == kHostId && bob.PicId_cai() == kHostId, "");
        bob.TakeEvents_cai();
        bob.RequestControl_cai(why);
        bool observerRefused = false;
        lmbda_pump(3, lmbda_heard(bob, "observers", observerRefused));
        Check_cai("an observer cannot get control", observerRefused, "");

        // 10. A notice.
        ann.SendNotice_cai("gear down?", why);
        bool heard = false;
        lmbda_pump(3, lmbda_heard(bob, "Ann: gear down?", heard));
        Check_cai("Bob reads Ann's notice", heard, "");

        // 11. The master takes control back from the PIC; PIC leaves -> host.
        host.SetRole_cai(bob.MyId_cai(), Role::Crew, why);
        ann.RequestControl_cai(why);
        lmbda_pump(3, lmbda_hasQuestion(host));
        host.AnswerControl_cai(ann.MyId_cai(), true, why);
        lmbda_pump(3, lmbda_pic(host, ann.MyId_cai()));
        Check_cai("the master takes control", host.TakeControl_cai(why) && lmbda_pump(3, lmbda_pic(ann, kHostId)), why);

        // 11b. Shared commands: Ann presses #3 -> Bob and the host run it, by Ann.
        auto lmbda_gotCommand = [](NetSession& s, std::uint16_t index, CommandPhase phase, std::uint8_t author,
                                   std::vector<RemoteCommand>& seen) {
            auto lmbda_has = [&s, index, phase, author, &seen]() {
                for (const RemoteCommand& c : s.TakeCommands_cai())
                    seen.push_back(c);
                for (const RemoteCommand& c : seen)
                    if (c.index == index && c.phase == phase && c.author == author)
                        return true;
                return false;
            };
            return lmbda_has;
        };
        std::vector<RemoteCommand> bobCmds, hostCmds, annCmds;
        ann.SendCommand_cai(3, CommandPhase::Begin, why);
        ann.SendCommand_cai(3, CommandPhase::End, why);
        const bool bobGot = lmbda_pump(3, lmbda_gotCommand(bob, 3, CommandPhase::End, ann.MyId_cai(), bobCmds));
        const bool hostGot = lmbda_pump(1, lmbda_gotCommand(host, 3, CommandPhase::End, ann.MyId_cai(), hostCmds));
        Check_cai("a command from Ann reaches Bob and the host (Begin + End, by Ann)", bobGot && hostGot && bobCmds.size() == 2,
                  fmt::format("bob {} host {}", bobCmds.size(), hostCmds.size()));
        lmbda_pump(0.3, nullptr);
        Check_cai("Ann does not get her own command back", ann.TakeCommands_cai().empty(), "");

        // Conflict: Ann (rank 3) presses #7, Bob (rank 4) presses #7 within 1 s -> Bob is rejected.
        bobCmds.clear();
        ann.SendCommand_cai(7, CommandPhase::Begin, why);
        lmbda_pump(3, lmbda_gotCommand(bob, 7, CommandPhase::Begin, ann.MyId_cai(), bobCmds));
        bob.SendCommand_cai(7, CommandPhase::Begin, why);
        bob.SendCommand_cai(7, CommandPhase::End, why);
        std::vector<Rejection> bobRejected;
        auto lmbda_rejected = [&bob, &bobRejected]() {
            for (const Rejection& r : bob.TakeRejections_cai())
                bobRejected.push_back(r);
            return !bobRejected.empty();
        };
        lmbda_pump(3, lmbda_rejected);
        annCmds.clear();
        lmbda_pump(0.5, lmbda_gotCommand(ann, 7, CommandPhase::Begin, bob.MyId_cai(), annCmds));
        Check_cai("within 1 s the lower rank is rejected (Bob after Ann), and not sent on",
                  !bobRejected.empty() && bobRejected.front().winner == ann.MyId_cai() && annCmds.empty(),
                  bobRejected.empty() ? std::string("no rejection") : bobRejected.front().text);
        ann.SendCommand_cai(7, CommandPhase::End, why);

        // 11c. Shared values: Ann changes #5 -> Bob gets it; Bob within 1 s -> corrected back.
        auto lmbda_gotValues = [](NetSession& s, RemoteValues::Kind kind, std::vector<RemoteValues>& seen) {
            auto lmbda_has = [&s, kind, &seen]() {
                for (RemoteValues& v : s.TakeValues_cai())
                    seen.push_back(std::move(v));
                for (const RemoteValues& v : seen)
                    if (v.kind == kind)
                        return true;
                return false;
            };
            return lmbda_has;
        };
        std::vector<RemoteValues> bobValues, annValues;
        ann.SendValues_cai({{5, 1.0}}, why);
        lmbda_pump(3, lmbda_gotValues(bob, RemoteValues::Kind::Change, bobValues));
        Check_cai("a value from Ann reaches Bob (by Ann)", !bobValues.empty() && bobValues.front().author == ann.MyId_cai() &&
                  bobValues.front().values.size() == 1 && bobValues.front().values[0].value == 1.0, "");
        bobValues.clear();
        bob.SendValues_cai({{5, 0.0}}, why);
        lmbda_pump(3, lmbda_gotValues(bob, RemoteValues::Kind::Correction, bobValues));
        lmbda_pump(0.3, lmbda_gotValues(ann, RemoteValues::Kind::Change, annValues));
        const bool corrected = !bobValues.empty() && bobValues.back().kind == RemoteValues::Kind::Correction &&
                               bobValues.back().values.size() == 1 && bobValues.back().values[0].value == 1.0;
        Check_cai("within 1 s Bob's value is corrected back to Ann's (and not sent on)", corrected && annValues.empty(),
                  bobValues.empty() ? std::string("no correction") : bobValues.back().text);

        // 11d. The master moves Bob above Ann: now Bob wins.
        Check_cai("the master moves Bob up in the priority", host.MovePriority_cai(bob.MyId_cai(), -1, why), why);
        auto lmbda_bobFirst = [&ann, &bob]() { return ann.PriorityOf_cai(bob.MyId_cai()) == 2; };
        lmbda_pump(3, lmbda_bobFirst);
        Check_cai("everybody sees Bob as priority 2, Ann 3", ann.PriorityOf_cai(bob.MyId_cai()) == 2 &&
                  bob.PriorityOf_cai(ann.MyId_cai()) == 3, "");
        annValues.clear();
        ann.SendValues_cai({{6, 3.0}}, why);
        lmbda_pump(3, lmbda_gotValues(bob, RemoteValues::Kind::Change, bobValues));
        bob.SendValues_cai({{6, 4.0}}, why);
        lmbda_pump(3, lmbda_gotValues(ann, RemoteValues::Kind::Change, annValues));
        Check_cai("with the higher priority Bob's change wins within 1 s", !annValues.empty() &&
                  annValues.back().author == bob.MyId_cai() && annValues.back().values[0].value == 4.0, "");
        host.MovePriority_cai(bob.MyId_cai(), 1, why);         // back to the join order

        // A held command of someone who leaves is ended for everybody (checked with Ann below).
        bobCmds.clear();
        ann.SendCommand_cai(9, CommandPhase::Begin, why);
        lmbda_pump(3, lmbda_gotCommand(bob, 9, CommandPhase::Begin, ann.MyId_cai(), bobCmds));

        // 12. Kick Ann.
        Check_cai("the master removes Ann", host.Kick_cai(ann.MyId_cai(), why), why);
        lmbda_pump(4, lmbda_ended(ann));
        Check_cai("Ann is out (removed by the master)", ann.State_cai() == SessionState::Lost &&
                  Utils::Contains_cai(ann.FailText_cai(), "removed"), ann.FailText_cai());
        Check_cai("Ann's held command #9 ends for Bob when she is removed",
                  lmbda_pump(3, lmbda_gotCommand(bob, 9, CommandPhase::End, 1, bobCmds)), "");
        lmbda_pump(3, lmbda_peopleCount(bob, 2));
        Check_cai("Bob sees 2 people", bob.People_cai().size() == 2, "");

        // 12b. While hosting: the next joiners start as observers; Bob keeps his seat.
        Check_cai("the host switches 'joiners start as' to Observer while hosting",
                  host.SetJoinersStartAs_cai(Role::Observer, why), why);
        cid.SetLocal_cai(DemoLocal_cai("Cid", acf, "100;1700000000", 0));
        cid.Join_cai(join);
        lmbda_pump(6, lmbda_state(cid, SessionState::InSession));
        lmbda_pump(3, lmbda_peopleCount(host, 3));            // the host admits Cid after its ProfileAck
        const Person* cidSeen = host.Find_cai(cid.MyId_cai());
        const Person* bobNow = host.Find_cai(bob.MyId_cai());
        Check_cai("Cid joins as Observer, Bob stays Crew",
                  cidSeen && cidSeen->role == Role::Observer && bobNow && bobNow->role == Role::Crew, cid.FailText_cai());
        {
            // The snapshot for a late joiner.
            std::vector<std::uint8_t> wanted = host.TakeSnapshotRequests_cai();
            const bool asked = std::find(wanted.begin(), wanted.end(), cid.MyId_cai()) != wanted.end();
            host.SendSnapshot_cai(cid.MyId_cai(), {{5, 1.0}, {6, 4.0}, {7, 0.5}});
            std::vector<RemoteValues> cidValues;
            lmbda_pump(3, lmbda_gotValues(cid, RemoteValues::Kind::Snapshot, cidValues));
            Check_cai("a late joiner gets the snapshot of the session's values",
                      asked && !cidValues.empty() && cidValues.front().values.size() == 3, "");
        }
        {
            std::string cidWhy;
            lmbda_pump(3, lmbda_peopleCount(cid, 3));          // Cid knows its own seat
            Check_cai("an observer cannot send shared commands", !cid.SendCommand_cai(2, CommandPhase::Begin, cidWhy), cidWhy);
            Check_cai("an observer cannot send shared values", !cid.SendValues_cai({{5, 9.0}}, cidWhy), cidWhy);
        }

        // 12c. Positions, weather, the snapshot again (protocol 4).
        {
            PositionInfo hostPos;
            hostPos.latitude = 32.0114; hostPos.longitude = 34.8867; hostPos.elevation = 40.0; hostPos.heading = 120.0f;
            hostPos.engines = 1; hostPos.engineCount = 1;
            PositionInfo bobPos = hostPos;
            bobPos.latitude += 0.001; bobPos.heading = 300.0f; bobPos.engines = 0;
            host.SendPosition_cai(hostPos);
            bob.SendPosition_cai(bobPos);
            auto lmbda_knows = [](NetSession& s, std::uint8_t id) {
                return [&s, id]() { return s.Positions_cai().count(id) > 0; };
            };
            lmbda_pump(3, lmbda_knows(bob, kHostId));
            lmbda_pump(3, lmbda_knows(cid, bob.MyId_cai()));
            Check_cai("everybody gets the host's position", bob.Positions_cai().count(kHostId) && cid.Positions_cai().count(kHostId) &&
                      std::abs(bob.Positions_cai().at(kHostId).position.latitude - 32.0114) < 1e-9 &&
                      bob.Positions_cai().at(kHostId).position.engines == 1, "");
            Check_cai("the host sends a joiner's position on to the others",
                      host.Positions_cai().count(bob.MyId_cai()) && cid.Positions_cai().count(bob.MyId_cai()) &&
                      cid.Positions_cai().at(bob.MyId_cai()).position.heading == 300.0f, "");
            const double metres = Utils::DistanceM_cai(hostPos.latitude, hostPos.longitude, bobPos.latitude, bobPos.longitude);
            Check_cai("0.001 degree of latitude is about 111 m", metres > 110.0 && metres < 112.5, fmt::format("{:.2f} m", metres));
            Check_cai("headings 350 and 10 are 20 degrees apart", std::abs(Utils::HeadingDiff_cai(350.0, 10.0) - 20.0) < 1e-9, "");

            host.SendWeather_cai({{0, 9.5}, {1, 101325.0}, {2, 15.0}});
            std::optional<std::vector<DataValue>> weather;
            auto lmbda_weather = [&bob, &weather]() {
                if (!weather)
                    weather = bob.TakeWeather_cai();
                return weather.has_value();
            };
            lmbda_pump(3, lmbda_weather);
            Check_cai("joiners get the host's weather", weather && weather->size() == 3 && (*weather)[1].value == 101325.0, "");
            Check_cai("a joiner cannot send weather (nothing arrives at the host)", !host.TakeWeather_cai().has_value(), "");

            host.TakeSnapshotRequests_cai();
            Check_cai("a joiner asks for the snapshot again (reloaded aircraft)", bob.RequestSnapshot_cai(why), why);
            auto lmbda_asked = [&host, &bob]() {
                const std::vector<std::uint8_t> wanted = host.TakeSnapshotRequests_cai();
                return std::find(wanted.begin(), wanted.end(), bob.MyId_cai()) != wanted.end();
            };
            Check_cai("the host gets the snapshot request", lmbda_pump(3, lmbda_asked), "");
            host.TakeValues_cai();
            const bool hostSelf = host.RequestSnapshot_cai(why);
            const std::vector<RemoteValues> own = host.TakeValues_cai();
            Check_cai("the host's reloaded aircraft gets the session's values back", hostSelf && !own.empty() &&
                      own.front().kind == RemoteValues::Kind::Snapshot && own.front().values.size() >= 3, why);
        }

        // 12d. The session's rules (protocol 5).
        {
            Check_cai("a joiner gets the host's rules when it joins", cid.RulesFromHost_cai() && bob.RulesFromHost_cai(), "");
            SessionRules rules;
            rules.rate = 20;
            rules.weatherS = 30;
            rules.conflictMs = 2000;
            rules.groundM = 250;                                   // clamped to 100
            host.SetOwnRules_cai(rules);
            auto lmbda_rules = [&bob, &cid]() { return bob.Rules_cai().rate == 20 && cid.Rules_cai().conflictMs == 2000; };
            lmbda_pump(3, lmbda_rules);
            Check_cai("the host changes the rules while hosting: everybody has them at once",
                      bob.Rules_cai().rate == 20 && bob.Rules_cai().weatherS == 30 && bob.Rules_cai().conflictMs == 2000 &&
                      cid.Rules_cai().conflictMs == 2000 && host.Rules_cai().rate == 20,
                      NetSession::RulesText_cai(bob.Rules_cai()));
            Check_cai("the rules are kept inside their ranges", bob.Rules_cai().groundM == 100, fmt::format("{}", bob.Rules_cai().groundM));
            bob.SetOwnRules_cai(SessionRules{});                  // a joiner's own rules do not change the session's
            Check_cai("a joiner's own rules do not apply while it is joined", bob.Rules_cai().rate == 20, "");
            host.SetOwnRules_cai(SessionRules{});
            auto lmbda_defaults = [&bob]() { return bob.Rules_cai().rate == 10; };
            lmbda_pump(3, lmbda_defaults);
        }

        // 12e. Flying together: the PIC's inputs (unreliable stream) and pause (protocol 6).
        {
            if (host.PicId_cai() != kHostId)
                host.TakeControl_cai(why);
            lmbda_pump(3, lmbda_pic(bob, kHostId));
            FlightInputs in;
            in.yokePitch = 0.25f; in.yokeHeading = -0.5f; in.brakeLeft = 1.0f; in.speedbrake = 0.5f;
            in.trimElevator = 0.1f; in.flapRequest = 0.5f; in.propMode = {1, 3};
            in.throttle = {0.8f, 0.6f}; in.prop = {1.0f, 1.0f}; in.mixture = {0.9f, 0.7f};
            host.SendFlightInputs_cai(in);
            std::optional<FlightInputs> bobIn, cidIn;
            auto lmbda_inputs = [](NetSession& s, std::optional<FlightInputs>& got) {
                return [&s, &got]() { if (!got) got = s.TakeFlightInputs_cai(); return got.has_value(); };
            };
            lmbda_pump(3, lmbda_inputs(bob, bobIn));
            lmbda_pump(3, lmbda_inputs(cid, cidIn));
            Check_cai("the PIC's flight inputs reach everybody (2 engines, speed brake, trims, flaps, reverse)", bobIn && cidIn && *bobIn == in && cidIn->throttle.size() == 2 &&
                      cidIn->throttle[1] == 0.6f && cidIn->propMode.size() == 2 && cidIn->propMode[1] == 3, "");
            bob.SendFlightInputs_cai(in);                          // not the PIC: nothing goes out
            lmbda_pump(0.5, nullptr);
            Check_cai("somebody who is not the PIC sends no flight inputs", !host.TakeFlightInputs_cai() && !cid.TakeFlightInputs_cai(), "");

            Check_cai("Bob asks for control", bob.RequestControl_cai(why), why);
            lmbda_pump(3, lmbda_hasQuestion(host));
            host.AnswerControl_cai(bob.MyId_cai(), true, why);
            lmbda_pump(3, lmbda_pic(bob, bob.MyId_cai()));
            lmbda_pump(3, lmbda_pic(cid, bob.MyId_cai()));
            in.throttle = {0.3f, 0.3f, 0.3f, 0.3f};
            bob.SendFlightInputs_cai(in);
            std::optional<FlightInputs> hostIn;
            cidIn.reset();
            lmbda_pump(3, lmbda_inputs(host, hostIn));
            lmbda_pump(3, lmbda_inputs(cid, cidIn));
            Check_cai("a joiner as PIC: its inputs reach the host and the others (4 engines)",
                      hostIn && hostIn->throttle.size() == 4 && cidIn && cidIn->throttle.size() == 4, "");

            bob.SendPause_cai(true);
            std::optional<bool> hostPause, cidPause;
            auto lmbda_pause = [](NetSession& s, std::optional<bool>& got) {
                return [&s, &got]() { if (!got) got = s.TakePause_cai(); return got.has_value(); };
            };
            lmbda_pump(3, lmbda_pause(host, hostPause));
            lmbda_pump(3, lmbda_pause(cid, cidPause));
            Check_cai("the PIC's pause reaches everybody", hostPause && *hostPause && cidPause && *cidPause, "");
            host.SendPause_cai(false);                             // the host is not the PIC now: ignored
            cidPause.reset();
            lmbda_pump(0.5, lmbda_pause(cid, cidPause));
            Check_cai("a pause from somebody who is not the PIC is not sent", !cidPause, "");
            bob.SendPause_cai(false);
            auto lmbda_resumed = [&cid]() { return cid.TakePause_cai().has_value(); };
            lmbda_pump(3, lmbda_resumed);
            host.TakeControl_cai(why);
            lmbda_pump(3, lmbda_pic(bob, kHostId));
        }

        // 12f. Copilot axes (opt-in rule, protocol 7): one Crew member on X-Plane's copilot side.
        {
            const CopilotInputs stick{0.2f, -0.4f, 0.1f};
            bob.SendCopilotInputs_cai(stick);                      // the rule is off: nothing goes out
            lmbda_pump(0.5, nullptr);
            Check_cai("copilot axes off: no copilot inputs are sent", !host.TakeCopilotInputs_cai() && !cid.TakeCopilotInputs_cai(), "");
            SessionRules rules;
            rules.copilotAxes = true;
            host.SetOwnRules_cai(rules);
            auto lmbda_copilotRule = [&bob, &cid]() { return bob.Rules_cai().copilotAxes && cid.Rules_cai().copilotAxes; };
            lmbda_pump(3, lmbda_copilotRule);
            Check_cai("Bob (Crew, not the PIC) may be the copilot; Cid (Observer) may not",
                      bob.MayCopilot_cai() && !cid.MayCopilot_cai() && !host.MayCopilot_cai(), "");
            bob.SendCopilotInputs_cai(stick);
            std::optional<NetSession::CopilotIn> hostCop, cidCop;
            auto lmbda_cop = [](NetSession& s, std::optional<NetSession::CopilotIn>& got) {
                return [&s, &got]() { if (!got) got = s.TakeCopilotInputs_cai(); return got.has_value(); };
            };
            lmbda_pump(3, lmbda_cop(host, hostCop));
            lmbda_pump(3, lmbda_cop(cid, cidCop));
            Check_cai("the copilot's stick reaches the PIC and the others", hostCop && hostCop->id == bob.MyId_cai() &&
                      hostCop->inputs == stick && cidCop && cidCop->inputs.roll == -0.4f, "");
            cid.SendCopilotInputs_cai(stick);                      // an observer: nothing goes out
            lmbda_pump(0.5, nullptr);
            Check_cai("an observer sends no copilot inputs", !host.TakeCopilotInputs_cai(), "");
            host.SetOwnRules_cai(SessionRules{});
            auto lmbda_copilotOff = [&bob]() { return !bob.Rules_cai().copilotAxes; };
            lmbda_pump(3, lmbda_copilotOff);
        }

        // 12g. Follow the PIC and the load (protocol 8). Bob is the PIC since 12e.
        {
            SessionRules rules;
            rules.flight = FlightMode::Inputs;
            rules.smoothingMs = 180;                               // snaps to 200
            rules.followStrict = true;
            rules.shareLoad = false;
            rules.handoverBlendMs = 1700;                          // snaps to 2000
            host.SetOwnRules_cai(rules);
            auto lmbda_inputsRule = [&cid]() { return cid.Rules_cai().flight == FlightMode::Inputs; };
            lmbda_pump(3, lmbda_inputsRule);
            Check_cai("the Follow rules reach the joiners (inputs only, 200 ms, strict, no load)",
                      cid.Rules_cai().flight == FlightMode::Inputs && cid.Rules_cai().smoothingMs == 200 &&
                      cid.Rules_cai().followStrict && !cid.Rules_cai().shareLoad && cid.Rules_cai().handoverBlendMs == 2000,
                      NetSession::RulesText_cai(cid.Rules_cai()));
            host.SetOwnRules_cai(SessionRules{});
            auto lmbda_followRule = [&cid, &bob]() { return cid.Rules_cai().flight == FlightMode::Follow && bob.Rules_cai().shareLoad; };
            lmbda_pump(3, lmbda_followRule);
            Check_cai("the default is Follow the PIC with fuel and payload, handover blend 1 s", cid.Rules_cai().flight == FlightMode::Follow &&
                      cid.Rules_cai().shareLoad && !cid.Rules_cai().followStrict && cid.Rules_cai().handoverBlendMs == 1000, "");

            AircraftState state;
            state.latitude = 45.59186966; state.longitude = -122.59684074; state.elevation = 8.25;
            state.heading = 133.4f; state.pitch = -1.5f; state.roll = 2.0f; state.vx = 3.0f; state.vy = 0.1f; state.vz = -2.0f;
            state.p = 0.5f; state.q = -0.25f; state.r = 12.0f; state.agl = 1.2f; state.onGround = true;
            auto lmbda_state = [](NetSession& s, std::optional<NetSession::StateIn>& got) {
                return [&s, &got]() { if (!got) got = s.TakeFlightState_cai(); return got.has_value(); };
            };
            auto lmbda_load = [](NetSession& s, std::optional<LoadInfo>& got) {
                return [&s, &got]() { if (!got) got = s.TakeLoad_cai(); return got.has_value(); };
            };
            // The host is the PIC (12e ended with it taking control).
            host.SendFlightState_cai(state, 123456);
            std::optional<NetSession::StateIn> bobState, cidState;
            lmbda_pump(3, lmbda_state(bob, bobState));
            lmbda_pump(3, lmbda_state(cid, cidState));
            Check_cai("the PIC's (host's) aircraft state reaches everybody, with its clock",
                      bobState && bobState->state == state && bobState->timeMs == 123456 && cidState && cidState->state == state, "");
            bob.SendFlightState_cai(state, 1);                     // not the PIC: nothing goes out
            lmbda_pump(0.5, nullptr);
            Check_cai("somebody who is not the PIC sends no aircraft state", !host.TakeFlightState_cai() && !cid.TakeFlightState_cai(), "");

            // A joiner as the PIC: the host relays its state and load.
            Check_cai("Bob asks for control (Follow test)", bob.RequestControl_cai(why), why);
            lmbda_pump(3, lmbda_hasQuestion(host));
            host.AnswerControl_cai(bob.MyId_cai(), true, why);
            lmbda_pump(3, lmbda_pic(cid, bob.MyId_cai()));
            state.heading = 140.0f;
            bob.SendFlightState_cai(state, 123500);
            std::optional<NetSession::StateIn> hostState;
            cidState.reset();
            lmbda_pump(3, lmbda_state(host, hostState));
            lmbda_pump(3, lmbda_state(cid, cidState));
            Check_cai("a joiner as PIC: its aircraft state reaches the host and the others",
                      hostState && hostState->state.heading == 140.0f && cidState && cidState->timeMs == 123500, "");
            Check_cai("the delay to the PIC comes from the pings", cid.PicDelayMs_cai() >= 0.0 && cid.PicDelayMs_cai() < 1000.0,
                      fmt::format("{:.1f} ms", cid.PicDelayMs_cai()));

            LoadInfo load;
            load.fuel = {80.5f, 79.0f};
            load.stations = {77.0f, 85.0f, 0.0f, 12.5f};
            load.payload = 174.5f; load.cgz = 0.05f; load.cgx = -0.01f; load.useStations = true;
            bob.SendLoad_cai(load);
            std::optional<LoadInfo> hostLoad, cidLoad;
            lmbda_pump(3, lmbda_load(host, hostLoad));
            lmbda_pump(3, lmbda_load(cid, cidLoad));
            Check_cai("the PIC's fuel and payload stations reach everybody", hostLoad && *hostLoad == load && cidLoad && *cidLoad == load, "");
            host.SendLoad_cai(load);                               // not the PIC
            lmbda_pump(0.5, nullptr);
            Check_cai("somebody who is not the PIC sends no load", !cid.TakeLoad_cai() && !bob.TakeLoad_cai(), "");
            host.TakeControl_cai(why);
            lmbda_pump(3, lmbda_pic(bob, kHostId));

            // The codec: a load with too many values is cut to kMaxLoadValues; a longer one on the wire is refused.
            LoadInfo big;
            big.fuel.assign(40, 1.0f);
            const Bytes bytes = EncodeMessage_cai(MsgLoad{1, big});
            std::string error;
            bool other = false;
            const auto decoded = DecodeMessage_cai(bytes.data(), bytes.size(), error, other);
            const auto* back = decoded ? std::get_if<MsgLoad>(&*decoded) : nullptr;
            Bytes bad = bytes;
            bad[kHeaderSize + 1] = 40;                             // the fuel count, past the limit
            const auto refused = DecodeMessage_cai(bad.data(), bad.size(), error, other);
            Check_cai("a load is cut to 32 values, and a longer one is refused", back && back->load.fuel.size() == kMaxLoadValues && !refused,
                      error);
        }

        // 13. The host ends the session.
        host.Leave_cai();
        lmbda_pump(4, lmbda_ended(bob));
        lmbda_pump(4, lmbda_ended(cid));
        Check_cai("Bob sees the session end", bob.State_cai() == SessionState::Lost &&
                  Utils::Contains_cai(bob.FailText_cai(), "host"), bob.FailText_cai());

        // 14. Nobody hosting: a join times out / fails.
        cid.Join_cai(join);
        lmbda_pump(12, lmbda_ended(cid));
        Check_cai("joining when nobody hosts fails with a clear text", cid.State_cai() == SessionState::Failed, cid.FailText_cai());

        for (NetSession* s : all)
            s->Leave_cai();
        Print_cai(fmt::format("=== {} passed, {} failed ===", mPassed, mFailed));
        return mFailed;
    }

private:
    // Prints one check.
    void Check_cai(const std::string& what, bool ok, const std::string& detail)
    {
        (ok ? mPassed : mFailed)++;
        Print_cai(fmt::format("[{}] {}{}", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  - " + detail));
        LOG_INFO("selftest {}: {}{}", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : " - " + detail);
    }

    int mPassed = 0;
    int mFailed = 0;
};

// Prints the options.
void Usage_cai()
{
    Print_cai("sc_net_test - Shared Cockpit network test");
    Print_cai("  --aircraft <file.acf | aircraft folder>   (reads its *.shared_cockpit.json, the same .acf stamp as the plugin)");
    Print_cai("  --name <n> --acf <file.acf> --stamp <s> --profile <*.shared_cockpit.json>");
    Print_cai("  --host [port] | --join <address[:port]>  --port <p> --password <p> --observer --max <2..4>");
    Print_cai("  --debug  --quiet (no log lines on the console)  --linger <s> (piped input: wait after it ends)");
    Print_cai("  --selftest");
    Print_cai("  The last values are kept in sc_net_test.ini next to the tool (the options above win).");
    Print_cai("  --fresh (start from the defaults)  --config <file> (another settings file)");
}

} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    bool selftest = false, doHost = false, doJoin = false, debug = false, fresh = false;
    bool aircraftGiven = false, profileGiven = false;
    fs::path configFile = ToolFolder_cai(argc > 0 ? argv[0] : nullptr) / "sc_net_test.ini";
    for (std::size_t i = 0; i < args.size(); ++i) {           // these two decide which file is read
        if (args[i] == "--fresh")
            fresh = true;
        else if (args[i] == "--config" && i + 1 < args.size())
            configFile = Utils::PathFromUtf8_cai(args[i + 1]);
    }
    int linger = 0;
    std::string profileFile, joinAddress, aircraftPath;
    std::string name = "Pilot";
    if (const char* user = std::getenv("USER"))
        name = user;
    else if (const char* winUser = std::getenv("USERNAME"))
        name = winUser;
    HostSettings host;
    JoinSettings join;
    std::string acf = "Cessna_172SP_G1000.acf", stamp = "test";
    // The last values (sc_net_test.ini); the command line below wins over them.
    gConfig.Load_cai(configFile);
    if (fresh)
        gConfig.Clear_cai();
    name = gConfig.Get_cai("name", name);
    acf = gConfig.Get_cai("acf", acf);
    stamp = gConfig.Get_cai("stamp", stamp);
    aircraftPath = gConfig.Get_cai("aircraft", "");
    profileFile = gConfig.Get_cai("profile", "");
    join.address = gConfig.Get_cai("join_address", "");
    join.port = gConfig.Port_cai("join_port", join.port);
    host.port = gConfig.Port_cai("host_port", host.port);
    Utils::ParseInt_cai(gConfig.Get_cai("max_people", "4"), host.maxPeople);
    host.maxPeople = std::clamp(host.maxPeople, 2, kMaxPeople);
    host.joinersStartAs = gConfig.Get_cai("joiners_start_as", "crew") == "observer" ? Role::Observer : Role::Crew;
    debug = gConfig.Get_cai("log_level", "info") == "debug";
    gConsoleLog = gConfig.Get_cai("console_log", "on") != "off";
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto lmbda_next = [&](std::string& out) {
            if (i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0) {
                out = args[++i];
                return true;
            }
            return false;
        };
        std::string value;
        if (a == "--selftest") selftest = true;
        else if (a == "--fresh") {}
        else if (a == "--config" && lmbda_next(value)) {}
        else if (a == "--debug") debug = true;
        else if (a == "--quiet") gConsoleLog = false;
        else if (a == "--observer") host.joinersStartAs = Role::Observer;
        else if (a == "--name" && lmbda_next(value)) name = value;
        else if (a == "--acf" && lmbda_next(value)) acf = value;
        else if (a == "--stamp" && lmbda_next(value)) stamp = value;
        else if (a == "--profile" && lmbda_next(value)) { profileFile = value; aircraftPath.clear(); profileGiven = true; }
        else if (a == "--aircraft" && lmbda_next(value)) { aircraftPath = value; profileFile.clear(); aircraftGiven = true; }
        else if (a == "--password" && lmbda_next(value)) host.password = join.password = value;
        else if (a == "--linger" && lmbda_next(value)) Utils::ParseInt_cai(value, linger);
        else if (a == "--max" && lmbda_next(value)) Utils::ParseInt_cai(value, host.maxPeople);
        else if (a == "--port" && lmbda_next(value)) {
            int port = 0;
            if (Utils::ParseInt_cai(value, port) && port > 0 && port < 65536)
                host.port = join.port = static_cast<std::uint16_t>(port);
        } else if (a == "--host") {
            doHost = true;
            if (lmbda_next(value)) {
                int port = 0;
                if (Utils::ParseInt_cai(value, port) && port > 0 && port < 65536)
                    host.port = static_cast<std::uint16_t>(port);
            }
        } else if (a == "--join" && lmbda_next(value)) {
            doJoin = true;
            joinAddress = value;
        } else if (a == "--help" || a == "-h") {
            Usage_cai();
            return 0;
        } else {
            Print_cai(fmt::format("unknown option '{}'", a));
            Usage_cai();
            return 2;
        }
    }

    log::SetMirrorSink_cai(ConsoleSink_cai);
    log::SetMirrorToXPlane_cai(true);
    log::Init_cai("sc_net_test", fs::current_path());
    log::SetLevel_cai(debug ? log::Level::Debug : log::Level::Info);
    LOG_INFO("sc_net_test {} (protocol {}), log file {}", kToolVersion, kProtocolVersion, Utils::Utf8_cai(log::GetLogFilePath_cai()));

    int result = 0;
    if (selftest) {
        result = SelfTest().Run_cai();
    } else {
        LocalInfo local = DemoLocal_cai(name, acf, stamp, 0);
        // A path from the command line must work; a remembered one that is gone is only a warning.
        auto lmbda_source = [&](bool aircraft, const std::string& path, bool given) {
            std::string error;
            if (aircraft ? LoadAircraft_cai(local, path, error) : LoadProfile_cai(local, path, error))
                return true;
            Print_cai(fmt::format("cannot use the {}: {}", aircraft ? "aircraft" : "profile", error));
            if (given)
                return false;
            Print_cai("  (remembered from last time: using the demo profile instead)");
            local = DemoLocal_cai(name, acf, stamp, 0);
            (aircraft ? aircraftPath : profileFile).clear();
            return true;
        };
        bool sourceOk = true;
        if (!aircraftPath.empty())
            sourceOk = lmbda_source(true, aircraftPath, aircraftGiven);
        else if (!profileFile.empty())
            sourceOk = lmbda_source(false, profileFile, profileGiven);
        if (!sourceOk) {
            log::Shutdown_cai();
            return 2;
        }
        if (doJoin) {
            const std::size_t colon = joinAddress.rfind(':');
            int port = 0;
            if (colon != std::string::npos && Utils::ParseInt_cai(joinAddress.substr(colon + 1), port) && port > 0 && port < 65536) {
                join.port = static_cast<std::uint16_t>(port);
                joinAddress = joinAddress.substr(0, colon);
            }
            join.address = joinAddress;
        }
        Console console(local, host, join);
        console.SetSources_cai(aircraftPath, aircraftPath.empty() ? profileFile : std::string());
        console.SetLinger_cai(linger);
        if (doHost)
            console.StartHost_cai();
        else if (doJoin)
            console.StartJoin_cai();
        result = console.Run_cai();
    }
    log::Shutdown_cai();
    return result;
}
