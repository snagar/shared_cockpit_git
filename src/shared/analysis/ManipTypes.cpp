// shared_cockpit - the manipulator types file (sc_manip_types.ini)

#include "shared/analysis/ManipTypes.h"

#include <algorithm>
#include <system_error>

#include "fmt/format.h"

#include "SharedInfo.h"
#include "shared/util/Utils.h"

namespace sc::analysis {

namespace fs = std::filesystem;

// The definition of `type`, or nullptr.
const ManipTypeDef* ManipTypeSet::Find_cai(std::string_view type) const
{
    for (const ManipTypeDef& def : types) {
        if (def.type == type)
            return &def;
    }
    return nullptr;
}

// True when one of the local words is in the refs or the tooltip.
bool ManipTypeSet::LocalByDefault_cai(const std::vector<std::string>& refs, std::string_view tooltip) const
{
    std::string text = Utils::Lower_cai(tooltip);
    for (const std::string& ref : refs)
        text += " " + Utils::Lower_cai(ref);
    auto lmbda_inText = [&text](const std::string& word) { return Utils::Contains_cai(text, word); };
    return std::any_of(localWords.begin(), localWords.end(), lmbda_inText);
}

// Parses the types file. Broken entries are skipped and reported.
ManipTypeSet ParseTypesIni_cai(std::string_view text)
{
    ManipTypeSet set;
    struct Key { std::string name, value; std::size_t line; };
    struct Section { std::string name; std::size_t line; std::vector<Key> keys; };
    std::vector<Section> sections;

    std::size_t lineNumber = 0;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t end = std::min(text.find('\n', at), text.size());
        std::string_view line = Utils::Trim_cai(text.substr(at, end - at));
        at = end + 1;
        ++lineNumber;
        if (lineNumber == 1 && line.starts_with("\xEF\xBB\xBF"))
            line.remove_prefix(3);
        if (line.empty() || line.front() == ';')
            continue;
        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            if (close == std::string_view::npos) {
                set.problems.push_back(fmt::format("line {}: a section line must be \"[name]\"", lineNumber));
                continue;
            }
            sections.push_back({Utils::Lower_cai(Utils::Trim_cai(line.substr(1, close - 1))), lineNumber, {}});
            continue;
        }
        const std::size_t equals = line.find('=');
        if (sections.empty() || equals == std::string_view::npos) {
            set.problems.push_back(fmt::format("line {}: not understood: {}", lineNumber, line));
            continue;
        }
        std::string_view value = line.substr(equals + 1);
        if (const std::size_t comment = value.find(" ;"); comment != std::string_view::npos)
            value = value.substr(0, comment);
        sections.back().keys.push_back({Utils::Lower_cai(Utils::Trim_cai(line.substr(0, equals))),
                                        std::string(Utils::Trim_cai(value)), lineNumber});
    }

    for (const Section& section : sections) {
        if (section.name == "file") {
            for (const Key& key : section.keys) {
                if (key.name != "version" || !Utils::ParseInt_cai(key.value, set.version))
                    set.problems.push_back(fmt::format("line {}: [file] takes only \"version = <number>\"", key.line));
            }
            continue;
        }
        if (section.name == "ignore" || section.name == "local_by_default") {
            const bool ignore = section.name == "ignore";
            for (const Key& key : section.keys) {
                if (key.name != (ignore ? "types" : "words")) {
                    set.problems.push_back(fmt::format("line {}: unknown key '{}' in [{}]", key.line, key.name, section.name));
                    continue;
                }
                for (const std::string& word : Utils::SplitWords_cai(key.value)) {
                    if (ignore)
                        set.ignored.insert(word);
                    else
                        set.localWords.push_back(Utils::Lower_cai(word));
                }
            }
            continue;
        }

        ManipTypeDef def;
        def.type = section.name.starts_with("attr_manip_") ? section.name.substr(11) : section.name;
        std::string why;
        bool haveParams = false, haveRefs = false, haveKind = false;
        for (const Key& key : section.keys) {
            if (key.name == "params") {
                haveParams = Utils::ParseInt_cai(key.value, def.params) && def.params >= 2 && def.params <= 32;
                if (!haveParams)
                    why = fmt::format("line {}: params must be a number from 2 to 32", key.line);
            } else if (key.name == "refs") {
                def.refs.clear();
                for (const std::string& word : Utils::SplitWords_cai(key.value)) {
                    int ref = 0;
                    if (!Utils::ParseInt_cai(word, ref)) {
                        why = fmt::format("line {}: refs must be numbers", key.line);
                        break;
                    }
                    def.refs.push_back(ref - 1);
                }
                haveRefs = true;
            } else if (key.name == "kind") {
                const std::string kind = Utils::Lower_cai(key.value);
                haveKind = true;
                if (kind == "command")      def.category = Category::Command;
                else if (kind == "dataref") def.category = Category::Dataref;
                else if (kind == "device")  def.category = Category::CantSync;
                else why = fmt::format("line {}: kind must be command, dataref or device", key.line);
            } else {
                why = fmt::format("line {}: unknown key '{}'", key.line, key.name);
            }
            if (!why.empty())
                break;
        }
        if (why.empty()) {
            if (!haveParams)    why = "params missing";
            else if (!haveKind) why = "kind missing";
            else if (!haveRefs || def.refs.empty() || def.refs.size() > 2) why = "refs needs 1 or 2 numbers";
            else {
                auto lmbda_outOfRange = [&def](int ref) { return ref < 1 || ref >= def.params; };
                if (std::any_of(def.refs.begin(), def.refs.end(), lmbda_outOfRange))
                    why = fmt::format("refs must be between 2 and params ({})", def.params);
            }
        }
        if (!why.empty()) {
            set.problems.push_back(fmt::format("[{}] (line {}): {} - skipped", section.name, section.line, why));
            continue;
        }
        if (set.Find_cai(def.type)) {
            set.problems.push_back(fmt::format("[{}] (line {}) appears twice - the first one is used", section.name,
                                               section.line));
            continue;
        }
        set.types.push_back(def);
    }
    for (const ManipTypeDef& def : set.types)
        set.ignored.erase(def.type);                      // defined wins over ignored
    if (set.types.empty())
        set.problems.push_back("no usable manipulator type");
    if (set.version != info::kTypesFileVersion)
        set.problems.push_back(fmt::format("the file is version {}, this plugin reads version {}", set.version,
                                           info::kTypesFileVersion));
    return set;
}

// Reads custom_<name> when it exists in `folder`, else <name>.
ManipTypeSet LoadTypesFile_cai(const fs::path& folder, fs::path& usedFile)
{
    const fs::path custom  = folder / ("custom_" + std::string(info::kTypesFile));
    const fs::path shipped = folder / std::string(info::kTypesFile);
    std::error_code ec;
    usedFile = fs::exists(custom, ec) ? custom : (fs::exists(shipped, ec) ? shipped : fs::path());
    if (usedFile.empty()) {
        ManipTypeSet set;
        set.problems.push_back(fmt::format("{} is missing from the plugin folder - reinstall the plugin",
                                           info::kTypesFile));
        return set;
    }
    return ParseTypesIni_cai(Utils::ReadAll_cai(usedFile));
}

} // namespace sc::analysis
