// shared_cockpit - the analysis of one aircraft, in plain C++ types
//
// This is what the Analyse page shows and edits, and what is saved to
// <aircraft folder>/<acf name>.shared_cockpit.json. Only standard types here (no json, no
// ImGui, no X-Plane), so the file format can change (JSON, INI, ...) without
// touching anything that uses the analysis: see shared/store/AnalysisStore.h.
//
// Controls are grouped by manipulator type. "Not used" types (none, noop,
// wheel, keyframe) are not listed, only counted in Signals::notUsed.

#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sc::analysis {

// What a manipulator type does, for sharing.
enum class Category {
    Command,     // runs commands
    Dataref,     // writes datarefs
    CantSync,    // owned by the aircraft (ATTR_manip_device touchscreens)
    NotUsed,     // no control of its own (none, noop, wheel, keyframe)
};

// How well one system can be shared.
enum class Grade { Full, Mostly, Partial, None, Check };

// The aircraft as a whole.
enum class Overall { Good, Limited, Poor };

// One control (one manipulator, its click areas merged).
struct Control {
    std::optional<bool>      share;      // true = shared, false = kept local, empty = can't be shared
    std::vector<std::string> refs;       // its commands / datarefs (identity, with the type)
    std::string              tip;        // tooltip (may be empty)
    std::string              file;       // the OBJ file it was found in (name only)
};

// All controls of one manipulator type.
struct TypeGroup {
    std::string          type;           // "command_knob" (without ATTR_manip_)
    Category             category = Category::Command;
    std::size_t          count = 0;      // controls found (items.size() when all are kept)
    std::vector<Control> items;
};

struct SystemGrade {
    std::string system;                  // switches | engines | radios | autopilot | flightplan | displays
    Grade       grade = Grade::Check;
    std::string reason;
};

struct Grades {
    std::string              source = "predicted";   // predicted (from the files) | measured (test mode, later)
    Overall                  overall = Overall::Limited;
    std::string              overallReason;
    std::vector<SystemGrade> systems;
};

// The counts the grades were calculated from (so old files can be graded again).
struct Signals {
    std::size_t                        commandsSim = 0;      // unique commands starting with "sim/"
    std::size_t                        commandsCustom = 0;   // unique commands of the aircraft / add-ons
    std::size_t                        datarefsSim = 0;
    std::size_t                        datarefsCustom = 0;
    std::size_t                        deviceManips = 0;     // touchscreens
    std::map<std::string, std::size_t> notUsed;              // type -> click areas
    std::vector<std::string>           pluginsFound;         // plugin folders in <aircraft>/plugins
    std::vector<std::string>           avionics;             // g1000, gns, laminar_fms, custom_cdu
};

// One file that was read.
struct FileEntry {
    std::string path;                    // relative to the aircraft folder, '/' separators
    std::string stamp;                   // Utils::FileStamp_cai
    std::size_t manipulators = 0;        // click areas (ATTR_manip_ lines) in it
};

struct AnalysisData {
    int                    format = 0;
    std::string            pluginVersion;
    std::string            analysed;     // local time, "2026-11-22T19:41:00"
    std::string            aircraftName;
    std::string            acf;          // the .acf file name
    std::string            acfStamp;
    std::vector<FileEntry> files;
    Grades                 grades;
    Signals                signals;
    std::vector<TypeGroup> controls;     // in the types file order

    // Controls that can be shared (commands and datarefs) and how many are ticked.
    std::size_t ShareableCount_cai() const;
    std::size_t SharedCount_cai() const;
    // Every control, can't-sync ones included.
    std::size_t ControlCount_cai() const;
};

// "command" / "dataref" / "cant" / "not_used" - the names used in files.
const char* CategoryName_cai(Category category);
std::optional<Category> ParseCategory_cai(std::string_view text);
// "Commands", "Datarefs", "Can't sync", "Not used" - for people.
const char* CategoryLabel_cai(Category category);

// "full" / "mostly" / "partial" / "none" / "check".
const char* GradeName_cai(Grade grade);
std::optional<Grade> ParseGrade_cai(std::string_view text);
// "Full", "Mostly", "Partial", "None", "Check in sim".
const char* GradeLabel_cai(Grade grade);

// "good" / "limited" / "poor".
const char* OverallName_cai(Overall overall);
std::optional<Overall> ParseOverall_cai(std::string_view text);
// "Good", "Limited", "Poor".
const char* OverallLabel_cai(Overall overall);

// "Switches & levers", "Flight plan / FMS", ... for a system name.
const char* SystemLabel_cai(std::string_view system);

// A control's identity: "<type> <ref1> [<ref2>]" (as vrconfig's BEGIN_MANIP).
std::string ControlKey_cai(std::string_view type, const std::vector<std::string>& refs);

// The keys (ControlKey_cai) of the controls that are ticked for sharing, sorted.
std::vector<std::string> SharedKeys_cai(const AnalysisData& data);
// Every control key of the aircraft (shared or not), sorted.
std::vector<std::string> AllKeys_cai(const AnalysisData& data);
// A hash of what is shared (the sorted shared keys): two people with the same hash
// share exactly the same controls. Empty for an analysis without shared controls.
std::string ProfileHash_cai(const AnalysisData& data);
// How many of `wanted` keys are not in `have` (both sorted).
std::size_t CountMissing_cai(const std::vector<std::string>& wanted, const std::vector<std::string>& have);

// One shared command, as numbered for the session (its place in SharedCommands_cai).
struct SharedCommand {
    std::string name;                    // "sim/electrical/battery_1_on"
    std::string label;                   // "BATTERY 1 ON" (the tooltip when the analysis has one)
};
// The commands of the ticked command-type controls, unique and sorted by name. Everybody in a
// session builds it from the same analysis, so a command travels as its index (2 bytes).
std::vector<SharedCommand> SharedCommands_cai(const AnalysisData& data);
// One shared dataref (or one element of an array dataref), numbered for the session.
struct SharedDataref {
    std::string ref;                     // as in the analysis: "sim/cockpit2/switches/fuel_pump_on[1]"
    std::string name;                    // the dataref: "sim/cockpit2/switches/fuel_pump_on"
    int         element = -1;            // the array element, -1 = not an array element
    std::string label;                   // "FUEL PUMP ON [1]" (the tooltip when the analysis has one)
};
// The datarefs of the ticked dataref-type controls, unique and sorted (index = its number).
std::vector<SharedDataref> SharedDatarefs_cai(const AnalysisData& data);

// A short name for a command / dataref: its last part, '_' as spaces, upper case
// ("sim/lights/landing_lights_toggle" -> "LANDING LIGHTS TOGGLE").
std::string ShortName_cai(std::string_view ref);

// The group a control belongs to in the "System" view: its first ref without the
// last part ("sim/electrical/battery_1_on" -> "sim/electrical"); "(screens)" when
// it has no '/'.
std::string SystemGroup_cai(const std::vector<std::string>& refs);

// A short name for people: the tooltip, else the last part of the first ref.
std::string ControlName_cai(const Control& control, std::string_view type);

} // namespace sc::analysis
