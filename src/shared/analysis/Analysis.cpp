// shared_cockpit - the analysis of one aircraft, in plain C++ types

#include "shared/analysis/Analysis.h"

#include "shared/util/Utils.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace sc::analysis {

// Controls that can be shared (commands and datarefs).
std::size_t AnalysisData::ShareableCount_cai() const
{
    std::size_t count = 0;
    for (const TypeGroup& group : controls) {
        if (group.category == Category::Command || group.category == Category::Dataref)
            count += group.items.size();
    }
    return count;
}

// Controls that are ticked to be shared.
std::size_t AnalysisData::SharedCount_cai() const
{
    std::size_t count = 0;
    for (const TypeGroup& group : controls) {
        for (const Control& control : group.items)
            count += control.share.value_or(false) ? 1 : 0;
    }
    return count;
}

// Every listed control.
std::size_t AnalysisData::ControlCount_cai() const
{
    std::size_t count = 0;
    for (const TypeGroup& group : controls)
        count += group.items.size();
    return count;
}

// The file name of a category.
const char* CategoryName_cai(Category category)
{
    switch (category) {
        case Category::Command:  return "command";
        case Category::Dataref:  return "dataref";
        case Category::CantSync: return "cant";
        case Category::NotUsed:  return "not_used";
    }
    return "";
}

// A category from its file name.
std::optional<Category> ParseCategory_cai(std::string_view text)
{
    for (Category category : {Category::Command, Category::Dataref, Category::CantSync, Category::NotUsed}) {
        if (text == CategoryName_cai(category))
            return category;
    }
    return std::nullopt;
}

// The name of a category for people.
const char* CategoryLabel_cai(Category category)
{
    switch (category) {
        case Category::Command:  return "Commands";
        case Category::Dataref:  return "Datarefs";
        case Category::CantSync: return "Can't sync";
        case Category::NotUsed:  return "Not used";
    }
    return "";
}

// The file name of a grade.
const char* GradeName_cai(Grade grade)
{
    switch (grade) {
        case Grade::Full:    return "full";
        case Grade::Mostly:  return "mostly";
        case Grade::Partial: return "partial";
        case Grade::None:    return "none";
        case Grade::Check:   return "check";
    }
    return "";
}

// A grade from its file name.
std::optional<Grade> ParseGrade_cai(std::string_view text)
{
    for (Grade grade : {Grade::Full, Grade::Mostly, Grade::Partial, Grade::None, Grade::Check}) {
        if (text == GradeName_cai(grade))
            return grade;
    }
    return std::nullopt;
}

// The name of a grade for people.
const char* GradeLabel_cai(Grade grade)
{
    switch (grade) {
        case Grade::Full:    return "Full";
        case Grade::Mostly:  return "Mostly";
        case Grade::Partial: return "Partial";
        case Grade::None:    return "None";
        case Grade::Check:   return "Check in sim";
    }
    return "";
}

// The file name of an overall grade.
const char* OverallName_cai(Overall overall)
{
    switch (overall) {
        case Overall::Good:    return "good";
        case Overall::Limited: return "limited";
        case Overall::Poor:    return "poor";
    }
    return "";
}

// An overall grade from its file name.
std::optional<Overall> ParseOverall_cai(std::string_view text)
{
    for (Overall overall : {Overall::Good, Overall::Limited, Overall::Poor}) {
        if (text == OverallName_cai(overall))
            return overall;
    }
    return std::nullopt;
}

// The name of an overall grade for people.
const char* OverallLabel_cai(Overall overall)
{
    switch (overall) {
        case Overall::Good:    return "Good";
        case Overall::Limited: return "Limited";
        case Overall::Poor:    return "Poor";
    }
    return "";
}

// The name of a system for people.
const char* SystemLabel_cai(std::string_view system)
{
    if (system == "switches")   return "Switches & levers";
    if (system == "engines")    return "Engines & electrical";
    if (system == "radios")     return "Radios";
    if (system == "autopilot")  return "Autopilot";
    if (system == "flightplan") return "Flight plan / FMS";
    if (system == "displays")   return "Displays";
    return "Other";
}

// "<type> <ref1> [<ref2>]".
std::string ControlKey_cai(std::string_view type, const std::vector<std::string>& refs)
{
    std::string key(type);
    for (const std::string& ref : refs)
        key += " " + ref;
    return key;
}

// The "System" view group of a control.
std::string SystemGroup_cai(const std::vector<std::string>& refs)
{
    if (refs.empty())
        return "(screens)";
    std::string_view ref = refs.front();
    const std::size_t bracket = ref.find('[');
    if (bracket != std::string_view::npos)
        ref = ref.substr(0, bracket);
    const std::size_t slash = ref.rfind('/');
    return slash == std::string_view::npos ? std::string("(screens)") : std::string(ref.substr(0, slash));
}

// The tooltip, else the last part of the first ref, else the type.
std::string ControlName_cai(const Control& control, std::string_view type)
{
    if (!control.tip.empty())
        return control.tip;
    if (control.refs.empty())
        return std::string(type);
    const std::string& ref = control.refs.front();
    const std::size_t slash = ref.find_last_of('/');
    return slash == std::string::npos || slash + 1 == ref.size() ? ref : ref.substr(slash + 1);
}

// The keys of the ticked controls, sorted.
std::vector<std::string> SharedKeys_cai(const AnalysisData& data)
{
    std::vector<std::string> keys;
    for (const TypeGroup& group : data.controls)
        for (const Control& control : group.items)
            if (control.share.value_or(false))
                keys.push_back(ControlKey_cai(group.type, control.refs));
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

// Every control key, sorted.
std::vector<std::string> AllKeys_cai(const AnalysisData& data)
{
    std::vector<std::string> keys;
    for (const TypeGroup& group : data.controls)
        for (const Control& control : group.items)
            keys.push_back(ControlKey_cai(group.type, control.refs));
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

// A hash of the sorted shared keys.
std::string ProfileHash_cai(const AnalysisData& data)
{
    const std::vector<std::string> keys = SharedKeys_cai(data);
    if (keys.empty())
        return {};
    std::string text;
    for (const std::string& key : keys) {
        text += key;
        text += '\n';
    }
    return Utils::Hash_cai(text);
}

// How many of `wanted` are not in `have` (both sorted).
std::size_t CountMissing_cai(const std::vector<std::string>& wanted, const std::vector<std::string>& have)
{
    std::size_t missing = 0;
    for (const std::string& key : wanted)
        if (!std::binary_search(have.begin(), have.end(), key))
            ++missing;
    return missing;
}

// A short name: the last part of a ref, '_' as spaces, upper case.
std::string ShortName_cai(std::string_view ref)
{
    const std::size_t slash = ref.rfind('/');
    std::string name(slash == std::string_view::npos ? ref : ref.substr(slash + 1));
    for (char& c : name)
        c = c == '_' ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return name;
}

// The shared commands, unique, sorted by name.
std::vector<SharedCommand> SharedCommands_cai(const AnalysisData& data)
{
    std::map<std::string, std::string> byName;           // name -> label
    for (const TypeGroup& group : data.controls) {
        if (group.category != Category::Command)
            continue;
        for (const Control& control : group.items) {
            if (!control.share.value_or(false))
                continue;
            for (const std::string& ref : control.refs) {
                if (ref.find('/') == std::string::npos)
                    continue;
                auto& label = byName[ref];
                // The tooltip names a control with one command well; with several (a knob's up
                // and down) the command's own name says which one.
                if (label.empty())
                    label = control.refs.size() == 1 && !control.tip.empty() ? control.tip : ShortName_cai(ref);
            }
        }
    }
    std::vector<SharedCommand> out;
    out.reserve(byName.size());
    for (auto& [name, label] : byName)
        out.push_back({name, label});
    return out;
}

// The shared datarefs, unique, sorted by ref.
std::vector<SharedDataref> SharedDatarefs_cai(const AnalysisData& data)
{
    std::map<std::string, std::string> byRef;           // ref -> label
    for (const TypeGroup& group : data.controls) {
        if (group.category != Category::Dataref)
            continue;
        for (const Control& control : group.items) {
            if (!control.share.value_or(false))
                continue;
            for (const std::string& ref : control.refs) {
                if (ref.find('/') == std::string::npos)
                    continue;
                auto& label = byRef[ref];
                if (label.empty())
                    label = control.refs.size() == 1 && !control.tip.empty() ? control.tip : ShortName_cai(ref);
            }
        }
    }
    std::vector<SharedDataref> out;
    out.reserve(byRef.size());
    for (auto& [ref, label] : byRef) {
        SharedDataref shared;
        shared.ref = ref;
        shared.name = ref;
        shared.label = label;
        // "name[3]": an element of an array dataref.
        const std::size_t open = ref.rfind('[');
        if (open != std::string::npos && ref.back() == ']' && open + 2 <= ref.size() - 1) {
            int element = -1;
            if (Utils::ParseInt_cai(std::string_view(ref).substr(open + 1, ref.size() - open - 2), element) && element >= 0) {
                shared.name = ref.substr(0, open);
                shared.element = element;
            }
        }
        out.push_back(std::move(shared));
    }
    return out;
}

} // namespace sc::analysis
