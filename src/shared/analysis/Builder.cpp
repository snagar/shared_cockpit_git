// shared_cockpit - turns a scan into an analysis, and grades it

#include "shared/analysis/Builder.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string_view>

#include "fmt/format.h"

#include "SharedInfo.h"
#include "shared/util/Utils.h"

namespace sc::analysis {

namespace {

// True for Laminar's own commands and datarefs.
bool IsSim_cai(std::string_view ref)
{
    return ref.starts_with("sim/");
}

// True when `text` starts with one of `prefixes`.
template <std::size_t N>
bool StartsWithAny_cai(std::string_view text, const std::array<std::string_view, N>& prefixes)
{
    auto lmbda_startsWith = [text](std::string_view prefix) { return text.starts_with(prefix); };
    return std::any_of(prefixes.begin(), prefixes.end(), lmbda_startsWith);
}

// True when `text` contains one of `words`.
template <std::size_t N>
bool ContainsAny_cai(std::string_view text, const std::array<std::string_view, N>& words)
{
    auto lmbda_contains = [text](std::string_view word) { return Utils::Contains_cai(text, word); };
    return std::any_of(words.begin(), words.end(), lmbda_contains);
}

// For Laminar's Garmin commands ("sim/GPS/g1000n1_hdg_up"): the part after
// "g1000n1_" / "g430n1_"; empty for anything else.
std::string_view GarminAction_cai(std::string_view ref)
{
    if (!ref.starts_with("sim/GPS/g1000n") && !ref.starts_with("sim/GPS/g430n"))
        return {};
    const std::size_t underscore = ref.find('_', 8);
    return underscore == std::string_view::npos ? std::string_view() : ref.substr(underscore + 1);
}

// The system a command / dataref belongs to besides "switches": autopilot,
// radios, engines, or empty.
std::string_view SystemOfRef_cai(std::string_view ref)
{
    static constexpr std::array<std::string_view, 3> kApSim   = {"sim/autopilot/", "sim/cockpit2/autopilot/", "sim/cockpit/autopilot/"};
    static constexpr std::array<std::string_view, 8> kApWords = {"autopilot", "/ap/", "ap_", "fcp", "fgcp", "fcu", "mcp", "flight_dir"};
    static constexpr std::array<std::string_view, 6> kRadioSim = {"sim/radios/", "sim/audio_panel", "sim/transponder/",
                                                                  "sim/cockpit2/radios/", "sim/cockpit/radios/", "sim/GPS/gcu478/com"};
    static constexpr std::array<std::string_view, 9> kRadioWords = {"radio", "com1", "com2", "nav1", "nav2", "xpdr",
                                                                    "transponder", "adf", "audio"};
    static constexpr std::array<std::string_view, 16> kEngineSim = {
        "sim/engines/", "sim/starters/", "sim/magnetos/", "sim/ignition/", "sim/igniters/", "sim/fuel/",
        "sim/electrical/", "sim/cockpit2/engine/", "sim/cockpit2/electrical/", "sim/cockpit2/fuel/",
        "sim/cockpit/engine/", "sim/cockpit/electrical/", "sim/bleed_air/", "sim/altair/", "sim/fadec/",
        "sim/cockpit2/ice/"};
    static constexpr std::array<std::string_view, 14> kEngineWords = {
        "engine", "eng_", "fuel", "elec", "starter", "ignit", "magneto", "apu", "bleed", "battery",
        "generator", "alternator", "throttle", "mixture"};
    static constexpr std::array<std::string_view, 11> kGarminAp = {"ap", "fd", "yd", "hdg", "alt", "nav", "vnv", "apr",
                                                                   "bc", "vs", "flc"};

    const std::string_view action = GarminAction_cai(ref);
    if (!action.empty()) {
        if (std::find(kGarminAp.begin(), kGarminAp.end(), action) != kGarminAp.end() || action.starts_with("nose_") ||
            action.starts_with("hdg_") || action.starts_with("alt_") || action.starts_with("crs_"))
            return "autopilot";
        if (action.starts_with("com") || action.starts_with("nav_") || action.starts_with("nav12") ||
            action.starts_with("cvol") || action.starts_with("nvol") || action.starts_with("vvol") ||
            action.starts_with("coarse") || action.starts_with("fine"))
            return "radios";
        return {};
    }
    if (IsSim_cai(ref)) {
        if (StartsWithAny_cai(ref, kApSim))     return "autopilot";
        if (StartsWithAny_cai(ref, kRadioSim))  return "radios";
        if (StartsWithAny_cai(ref, kEngineSim)) return "engines";
        return {};
    }
    const std::string lower = Utils::Lower_cai(ref);
    if (ContainsAny_cai(lower, kApWords))     return "autopilot";
    if (ContainsAny_cai(lower, kRadioWords))  return "radios";
    if (ContainsAny_cai(lower, kEngineWords)) return "engines";
    return {};
}

// Unique refs of one system: how many in all, how many belong to the aircraft.
struct RefCount {
    std::set<std::string> all;
    std::size_t           custom = 0;

    // Adds one ref (counted once).
    void Add_cai(const std::string& ref)
    {
        if (all.insert(ref).second && !IsSim_cai(ref))
            ++custom;
    }
};

// The grade of a system from its refs. `what`: the reason when all are Laminar's.
SystemGrade GradeSystem_cai(std::string_view system, const RefCount& refs, bool hasPlugins, std::string_view what)
{
    SystemGrade grade;
    grade.system = std::string(system);
    const std::size_t total = refs.all.size();
    if (total == 0) {
        grade.grade  = Grade::Check;
        grade.reason = "Not found as cockpit controls";
    } else if (refs.custom == 0) {
        grade.grade  = Grade::Full;
        grade.reason = std::string(what);
    } else if (refs.custom * 2 <= total || system == "switches" || !hasPlugins) {
        grade.grade  = Grade::Mostly;
        grade.reason = fmt::format("{} of its {} commands / datarefs are custom to this aircraft (not X-Plane's sim/...)", refs.custom, total);
    } else {
        grade.grade  = Grade::Check;
        grade.reason = fmt::format("Run by the aircraft plugin: {} of its {} commands / datarefs are custom to this aircraft",
                                   refs.custom, total);
    }
    return grade;
}

// An add-on CDU has a keypad: this many key commands at least ("FMS_MSG" lights do not count).
constexpr std::size_t kMinCduKeys = 5;

// True for an add-on command that looks like a CDU / FMC key ("xyz/cdu1/key_A",
// "xyz/fmc/lsk_1l", "xyz/mcdu/exec").
bool IsCduKey_cai(std::string_view ref)
{
    const std::string lower = Utils::Lower_cai(ref);
    const bool unit = Utils::Contains_cai(lower, "cdu") || Utils::Contains_cai(lower, "fmc") ||
                      Utils::Contains_cai(lower, "/fms/") || Utils::Contains_cai(lower, "fms_key") ||
                      Utils::Contains_cai(lower, "fms1/") || Utils::Contains_cai(lower, "fms2/");
    return unit && !Utils::Contains_cai(lower, "msg") && !Utils::Contains_cai(lower, "popup");
}

// True when `avionics` lists `name`.
bool Has_cai(const std::vector<std::string>& avionics, std::string_view name)
{
    return std::find(avionics.begin(), avionics.end(), name) != avionics.end();
}

} // namespace

// Recounts the Signals that come from the controls.
void CountSignals_cai(AnalysisData& data)
{
    Signals& signals = data.signals;
    std::set<std::string> commandsSim, commandsCustom, datarefsSim, datarefsCustom;
    std::set<std::string> avionics;
    std::set<std::string> cduKeys;          // add-on CDU / FMC key commands
    signals.deviceManips = 0;
    for (const TypeGroup& group : data.controls) {
        if (group.category == Category::CantSync) {
            signals.deviceManips += group.items.size();
            continue;
        }
        for (const Control& control : group.items) {
            for (const std::string& ref : control.refs) {
                const bool sim = IsSim_cai(ref);
                if (group.category == Category::Command)
                    (sim ? commandsSim : commandsCustom).insert(ref);
                else
                    (sim ? datarefsSim : datarefsCustom).insert(ref);
                if (ref.starts_with("sim/GPS/g1000") || ref.starts_with("sim/GPS/gcu478"))
                    avionics.insert("g1000");
                else if (ref.starts_with("sim/GPS/g430"))
                    avionics.insert("gns");
                else if (ref.starts_with("sim/FMS"))
                    avionics.insert("laminar_fms");
                else if (!sim && group.category == Category::Command && IsCduKey_cai(ref))
                    cduKeys.insert(ref);
            }
        }
    }
    if (cduKeys.size() >= kMinCduKeys)
        avionics.insert("custom_cdu");
    signals.commandsSim    = commandsSim.size();
    signals.commandsCustom = commandsCustom.size();
    signals.datarefsSim    = datarefsSim.size();
    signals.datarefsCustom = datarefsCustom.size();
    signals.avionics.assign(avionics.begin(), avionics.end());
}

// Fills data.grades (predicted) from the controls and the Signals.
void GradeAnalysis_cai(AnalysisData& data)
{
    const Signals& signals = data.signals;
    const bool hasPlugins = !signals.pluginsFound.empty();
    std::map<std::string, RefCount> bySystem;
    for (const TypeGroup& group : data.controls) {
        if (group.category != Category::Command && group.category != Category::Dataref)
            continue;
        for (const Control& control : group.items) {
            for (const std::string& ref : control.refs) {
                bySystem["switches"].Add_cai(ref);
                if (const std::string_view system = SystemOfRef_cai(ref); !system.empty())
                    bySystem[std::string(system)].Add_cai(ref);
            }
        }
    }

    Grades grades;
    grades.source = "predicted";
    grades.systems.push_back(GradeSystem_cai("switches", bySystem["switches"], hasPlugins, "Laminar commands and datarefs"));
    grades.systems.push_back(GradeSystem_cai("engines", bySystem["engines"], hasPlugins, "Default X-Plane systems"));
    grades.systems.push_back(GradeSystem_cai("radios", bySystem["radios"], hasPlugins, "Laminar radio commands"));
    grades.systems.push_back(GradeSystem_cai("autopilot", bySystem["autopilot"], hasPlugins, "Laminar autopilot commands"));

    SystemGrade plan{"flightplan", Grade::Check, "No flight-plan controls found"};
    if (Has_cai(signals.avionics, "g1000"))
        plan = {"flightplan", Grade::Full, "Laminar G1000: SDK flight-plan API"};
    else if (Has_cai(signals.avionics, "gns"))
        plan = {"flightplan", Grade::Full, "Laminar GNS 430/530: SDK flight-plan API"};
    else if (Has_cai(signals.avionics, "laminar_fms"))
        plan = {"flightplan", Grade::Full, "Laminar FMS: SDK flight-plan API"};
    else if (Has_cai(signals.avionics, "custom_cdu"))
        plan = {"flightplan", Grade::Partial, "Add-on CDU: key presses replayed in order"};
    else if (signals.deviceManips > 0)
        plan = {"flightplan", Grade::None, "Entered on the aircraft's own screens"};
    grades.systems.push_back(plan);

    SystemGrade displays{"displays", Grade::Full, "No screens with a state of their own found"};
    if (signals.deviceManips > 0)
        displays = {"displays", Grade::None, fmt::format("{} owned by the aircraft plugin",
                                                          Utils::Plural_cai(signals.deviceManips, "touchscreen", "touchscreens"))};
    else if (Has_cai(signals.avionics, "g1000") || Has_cai(signals.avionics, "gns"))
        displays = {"displays", Grade::Mostly, "Garmin pages follow key presses, in order"};
    grades.systems.push_back(displays);

    std::vector<std::string> weak;
    bool poor = false;
    for (const SystemGrade& system : grades.systems) {
        if (system.grade == Grade::Full || system.grade == Grade::Mostly)
            continue;
        weak.push_back(SystemLabel_cai(system.system));
        if (system.grade == Grade::None && (system.system == "switches" || system.system == "engines"))
            poor = true;
    }
    if (poor) {
        grades.overall       = Overall::Poor;
        grades.overallReason = "The basic controls cannot be shared.";
    } else if (weak.empty()) {
        grades.overall       = Overall::Good;
        grades.overallReason = "Every system can be shared.";
    } else {
        std::string list;
        for (const std::string& name : weak)
            list += (list.empty() ? "" : ", ") + name;
        grades.overall       = Overall::Limited;
        grades.overallReason = fmt::format("Switches share.\nNot fully: {}.", list);
    }
    data.grades = std::move(grades);
}

// A new analysis from `scan`, keeping the ticks of `previous`.
AnalysisData BuildAnalysis_cai(const AircraftScan& scan, const ManipTypeSet& types, const std::string& aircraftName,
                               const AnalysisData* previous, BuildChanges* changes)
{
    AnalysisData data;
    data.format        = info::kAnalysisFormat;
    data.pluginVersion = std::string(info::kVersion);
    data.analysed      = Utils::NowIso_cai();
    data.aircraftName  = aircraftName;
    data.acf           = Utils::Utf8_cai(scan.acfFile.filename());
    data.acfStamp      = scan.acfStamp;

    const std::filesystem::path aircraftFolder = scan.acfFile.parent_path();
    for (const ScannedFile& file : scan.files) {
        if (!file.exists)
            continue;
        FileEntry entry;
        std::error_code ec;
        const std::filesystem::path relative = std::filesystem::relative(file.path, aircraftFolder, ec);
        entry.path         = ec || relative.empty() ? Utils::Utf8_cai(file.path.filename())
                                                    : Utils::Utf8_cai(relative.generic_u8string());
        entry.stamp        = file.stamp;
        entry.manipulators = file.manipulators;
        data.files.push_back(std::move(entry));
    }

    // The earlier ticks, by control key.
    std::map<std::string, bool> earlier;
    if (previous) {
        for (const TypeGroup& group : previous->controls) {
            for (const Control& control : group.items) {
                if (control.share)
                    earlier[ControlKey_cai(group.type, control.refs)] = *control.share;
            }
        }
    }

    // Groups in the types file order; controls in the order they were found.
    std::map<std::string, std::size_t> groupIndex;
    for (const ManipTypeDef& def : types.types) {
        groupIndex[def.type] = data.controls.size();
        data.controls.push_back({def.type, def.category, 0, {}});
    }
    for (const FoundControl& found : scan.controls) {
        const auto at = groupIndex.find(found.type);
        if (at == groupIndex.end())
            continue;
        TypeGroup& group = data.controls[at->second];
        Control control;
        control.refs = found.refs;
        control.tip  = found.tooltip;
        control.file = found.objFile;
        if (group.category == Category::Command || group.category == Category::Dataref) {
            const auto known = earlier.find(ControlKey_cai(group.type, found.refs));
            control.share = known != earlier.end() ? known->second : !types.LocalByDefault_cai(found.refs, found.tooltip);
        }
        group.items.push_back(std::move(control));
    }
    auto lmbda_isEmpty = [](const TypeGroup& group) { return group.items.empty(); };
    std::erase_if(data.controls, lmbda_isEmpty);
    for (TypeGroup& group : data.controls)
        group.count = group.items.size();

    if (previous && changes) {
        std::set<std::string> before, now;
        for (const TypeGroup& group : previous->controls)
            for (const Control& control : group.items)
                before.insert(ControlKey_cai(group.type, control.refs));
        for (const TypeGroup& group : data.controls)
            for (const Control& control : group.items)
                now.insert(ControlKey_cai(group.type, control.refs));
        for (const std::string& key : now) {
            if (before.contains(key)) ++changes->unchanged;
            else changes->added.push_back(key);
        }
        for (const std::string& key : before)
            if (!now.contains(key)) changes->removed.push_back(key);
    }

    data.signals.notUsed      = scan.notUsed;
    data.signals.pluginsFound = scan.pluginsFound;
    CountSignals_cai(data);
    GradeAnalysis_cai(data);
    return data;
}

// Explains in words, per system, what the grades mean for two pilots flying together.
std::vector<Explanation> ExplainSharing_cai(const AnalysisData& data)
{
    const Signals& signals = data.signals;
    const bool plugins = !signals.pluginsFound.empty();
    std::string pluginList;
    for (const std::string& name : signals.pluginsFound)
        pluginList += (pluginList.empty() ? "" : ", ") + name;

    std::vector<Explanation> out;
    for (const SystemGrade& system : data.grades.systems) {
        Explanation item;
        item.grade = system.grade;
        item.title = fmt::format("{} - {}", SystemLabel_cai(system.system), GradeLabel_cai(system.grade));
        std::string text;
        switch (system.grade) {
            case Grade::Full:
                text = "Everything here uses X-Plane's own commands and datarefs, which behave the same in both "
                       "simulators. What one pilot does, the other sees.";
                break;
            case Grade::Mostly:
                text = "Most controls use X-Plane's own commands and datarefs. Some belong to the aircraft itself; "
                       "they are shared the same way, but how the aircraft reacts to them cannot be checked from "
                       "the files.";
                break;
            case Grade::Partial:
                text = "These controls can only be shared by repeating every key press on the other side, in the "
                       "same order. One lost or doubled press puts the two simulators out of step, so check them "
                       "against each other now and then.";
                break;
            case Grade::None:
                text = "This part cannot be shared. Each pilot has to set it up on his own simulator.";
                break;
            case Grade::Check:
                text = "The files do not say enough. Try it in a session: work these controls on one side and "
                       "watch whether the other side follows.";
                break;
        }
        if (system.system == "flightplan") {
            if (system.grade == Grade::Full)
                text = "The flight plan is kept by X-Plane's own avionics, which the plugin can read and write "
                       "directly. A route entered on one side appears on the other.";
            else if (system.grade == Grade::None)
                text = "The flight plan is entered on the aircraft's own screens, which belong to its plugin. Both "
                       "pilots need to load or enter the same route.";
            else if (system.grade == Grade::Check)
                text = "No flight-plan controls were found. If the aircraft has a navigator, enter the route on "
                       "both sides.";
        } else if (system.system == "displays") {
            if (signals.deviceManips > 0)
                text = fmt::format("{} are drawn and run by the aircraft's own plugin. Touches on them cannot be "
                                   "seen by Shared Cockpit, so each pilot uses his own screens.",
                                   Utils::Plural_cai(signals.deviceManips, "touchscreen", "touchscreens"));
            else if (system.grade == Grade::Mostly)
                text = "The Garmin pages follow the buttons and knobs pressed on either side, in order. A page "
                       "can drift if a press is lost: press the page key again to line them up.";
        }
        if (system.grade == Grade::Check && plugins && system.system != "flightplan" && system.system != "displays")
            text += fmt::format(" The aircraft runs its own plugins ({}), which may keep this system's state "
                                "to themselves.", pluginList);
        if (system.system != "flightplan" && system.system != "displays")
            text += " [" + system.reason + "]";
        item.text = std::move(text);
        out.push_back(std::move(item));
    }
    return out;
}

} // namespace sc::analysis
