// shared_cockpit - the analysis file as JSON (nlohmann/json)
//
// The only file that includes nlohmann/json: everything is turned into the plain
// types of shared/analysis/Analysis.h here. Rules:
//   - ordered_json keeps the keys in the order they are written;
//   - each control is written on one line, so the file reads (and compares) line by line;
//   - comments in a file edited by hand are skipped when reading (lost on the next save);
//   - text that is not valid UTF-8 (old OBJ tooltips) is replaced, not thrown;
//   - no exception ever leaves this file.

#include "shared/store/AnalysisStore.h"

#include <exception>

#include "nlohmann/json.hpp"

#include "SharedInfo.h"

namespace sc::store {

namespace {

using Json = nlohmann::ordered_json;
using analysis::AnalysisData;

constexpr const char* kHelp = "Shared Cockpit analysis of this aircraft. share: true = shared with the other pilot, "
                              "false = kept local, null = cannot be shared. Comments are allowed but are lost when "
                              "the plugin saves the file.";

// `value` as one compact line (invalid UTF-8 replaced).
std::string Compact_cai(const Json& value)
{
    return value.dump(-1, ' ', false, Json::error_handler_t::replace);
}

// `value` pretty printed, every line after the first indented by `indent` spaces.
std::string Pretty_cai(const Json& value, int indent)
{
    const std::string text = value.dump(2, ' ', false, Json::error_handler_t::replace);
    std::string out;
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    for (char character : text) {
        out += character;
        if (character == '\n')
            out += pad;
    }
    return out;
}

// A string member, or `fallback` when missing or not a string.
std::string GetString_cai(const Json& object, const char* key, const std::string& fallback = {})
{
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
}

// A whole number member (>= 0), or `fallback`.
std::size_t GetCount_cai(const Json& object, const char* key, std::size_t fallback = 0)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer())
        return fallback;
    const auto value = it->get<long long>();
    return value < 0 ? fallback : static_cast<std::size_t>(value);
}

// A member that is an object, or an empty object.
const Json& GetObject_cai(const Json& object, const char* key)
{
    static const Json kEmpty = Json::object();
    const auto it = object.find(key);
    return it != object.end() && it->is_object() ? *it : kEmpty;
}

// A member that is an array, or an empty array.
const Json& GetArray_cai(const Json& object, const char* key)
{
    static const Json kEmpty = Json::array();
    const auto it = object.find(key);
    return it != object.end() && it->is_array() ? *it : kEmpty;
}

// The strings of an array member (other values skipped).
std::vector<std::string> GetStrings_cai(const Json& object, const char* key)
{
    std::vector<std::string> strings;
    for (const Json& item : GetArray_cai(object, key)) {
        if (item.is_string())
            strings.push_back(item.get<std::string>());
    }
    return strings;
}

class JsonAnalysisStore final : public AnalysisStore {
public:
    // The extension of this format.
    std::string Extension_cai() const override { return ".json"; }

    // Turns JSON text into an analysis.
    bool FromText_cai(const std::string& text, AnalysisData& data, std::string& error) const override
    {
        try {
            const Json root = Json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
            if (root.is_discarded() || !root.is_object()) {
                error = "not a valid JSON file";
                return false;
            }
            AnalysisData read;
            const Json& file = GetObject_cai(root, "file");
            read.format        = static_cast<int>(GetCount_cai(file, "format"));
            read.pluginVersion = GetString_cai(file, "plugin_version");
            read.analysed      = GetString_cai(file, "analysed");
            if (read.format < 1) {
                error = "\"file\": \"format\" is missing";
                return false;
            }
            if (read.format > info::kAnalysisFormat) {
                error = "made by a newer Shared Cockpit version (format " + std::to_string(read.format) + ")";
                return false;
            }
            const Json& aircraft = GetObject_cai(root, "aircraft");
            read.aircraftName = GetString_cai(aircraft, "name");
            read.acf          = GetString_cai(aircraft, "acf");
            read.acfStamp     = GetString_cai(aircraft, "acf_stamp");

            for (const Json& item : GetArray_cai(root, "files")) {
                if (!item.is_object())
                    continue;
                read.files.push_back({GetString_cai(item, "path"), GetString_cai(item, "stamp"),
                                      GetCount_cai(item, "manipulators")});
            }

            const Json& grades = GetObject_cai(root, "grades");
            read.grades.source = GetString_cai(grades, "source", "predicted");
            const Json& overall = GetObject_cai(grades, "overall");
            read.grades.overall       = analysis::ParseOverall_cai(GetString_cai(overall, "grade")).value_or(analysis::Overall::Limited);
            read.grades.overallReason = GetString_cai(overall, "reason");
            for (const Json& item : GetArray_cai(grades, "systems")) {
                if (!item.is_object())
                    continue;
                read.grades.systems.push_back({GetString_cai(item, "system"),
                                               analysis::ParseGrade_cai(GetString_cai(item, "grade")).value_or(analysis::Grade::Check),
                                               GetString_cai(item, "reason")});
            }

            const Json& signals = GetObject_cai(root, "signals");
            read.signals.commandsSim    = GetCount_cai(signals, "commands_sim");
            read.signals.commandsCustom = GetCount_cai(signals, "commands_custom");
            read.signals.datarefsSim    = GetCount_cai(signals, "datarefs_sim");
            read.signals.datarefsCustom = GetCount_cai(signals, "datarefs_custom");
            read.signals.deviceManips   = GetCount_cai(signals, "device_manips");
            for (const auto& [type, count] : GetObject_cai(signals, "not_used").items()) {
                if (count.is_number_integer() && count.get<long long>() >= 0)
                    read.signals.notUsed[type] = static_cast<std::size_t>(count.get<long long>());
            }
            read.signals.pluginsFound = GetStrings_cai(signals, "plugins_found");
            read.signals.avionics     = GetStrings_cai(signals, "avionics");

            for (const auto& [type, group] : GetObject_cai(root, "controls").items()) {
                if (!group.is_object())
                    continue;
                analysis::TypeGroup out;
                out.type     = type;
                out.category = analysis::ParseCategory_cai(GetString_cai(group, "category")).value_or(analysis::Category::Command);
                for (const Json& item : GetArray_cai(group, "items")) {
                    if (!item.is_object())
                        continue;
                    analysis::Control control;
                    const auto share = item.find("share");
                    if (share != item.end() && share->is_boolean())
                        control.share = share->get<bool>();
                    control.refs = GetStrings_cai(item, "refs");
                    control.tip  = GetString_cai(item, "tip");
                    control.file = GetString_cai(item, "file");
                    if (!control.refs.empty())
                        out.items.push_back(std::move(control));
                }
                out.count = GetCount_cai(group, "count", out.items.size());
                read.controls.push_back(std::move(out));
            }
            data = std::move(read);
            return true;
        } catch (const std::exception& exception) {
            error = std::string("cannot read the file: ") + exception.what();
        } catch (...) {
            error = "cannot read the file";
        }
        return false;
    }

    // Turns an analysis into JSON text: the header pretty printed, one line per control.
    std::string ToText_cai(const AnalysisData& data) const override
    {
        try {
            Json file = Json::object();
            file["format"]         = data.format;
            file["plugin_version"] = data.pluginVersion;
            file["analysed"]       = data.analysed;

            Json aircraft = Json::object();
            aircraft["name"]      = data.aircraftName;
            aircraft["acf"]       = data.acf;
            aircraft["acf_stamp"] = data.acfStamp;

            Json grades = Json::object();
            grades["source"]  = data.grades.source;
            grades["overall"] = Json{{"grade", analysis::OverallName_cai(data.grades.overall)},
                                     {"reason", data.grades.overallReason}};
            Json systems = Json::array();
            for (const analysis::SystemGrade& system : data.grades.systems)
                systems.push_back(Json{{"system", system.system}, {"grade", analysis::GradeName_cai(system.grade)},
                                       {"reason", system.reason}});
            grades["systems"] = std::move(systems);

            Json signals = Json::object();
            signals["commands_sim"]    = data.signals.commandsSim;
            signals["commands_custom"] = data.signals.commandsCustom;
            signals["datarefs_sim"]    = data.signals.datarefsSim;
            signals["datarefs_custom"] = data.signals.datarefsCustom;
            signals["device_manips"]   = data.signals.deviceManips;
            Json notUsed = Json::object();
            for (const auto& [type, count] : data.signals.notUsed)
                notUsed[type] = count;
            signals["not_used"]      = std::move(notUsed);
            signals["plugins_found"] = data.signals.pluginsFound;
            signals["avionics"]      = data.signals.avionics;

            std::string out = "{\n";
            out += "  \"_help\": " + Compact_cai(kHelp) + ",\n";
            out += "  \"file\": " + Compact_cai(file) + ",\n";
            out += "  \"aircraft\": " + Compact_cai(aircraft) + ",\n";
            out += "  \"files\": [";
            for (std::size_t i = 0; i < data.files.size(); ++i) {
                const analysis::FileEntry& entry = data.files[i];
                out += (i == 0 ? "\n    " : ",\n    ") +
                       Compact_cai(Json{{"path", entry.path}, {"stamp", entry.stamp}, {"manipulators", entry.manipulators}});
            }
            out += data.files.empty() ? "],\n" : "\n  ],\n";
            out += "  \"grades\": {\n    \"source\": " + Compact_cai(grades["source"]) + ",\n    \"overall\": " +
                   Compact_cai(grades["overall"]) + ",\n    \"systems\": [";
            for (std::size_t i = 0; i < grades["systems"].size(); ++i)
                out += (i == 0 ? "\n      " : ",\n      ") + Compact_cai(grades["systems"][i]);
            out += grades["systems"].empty() ? "]\n  },\n" : "\n    ]\n  },\n";
            out += "  \"signals\": " + Pretty_cai(signals, 2) + ",\n";
            out += "  \"controls\": {";
            for (std::size_t g = 0; g < data.controls.size(); ++g) {
                const analysis::TypeGroup& group = data.controls[g];
                out += (g == 0 ? "\n    " : ",\n    ") + Compact_cai(group.type) + ": { \"category\": " +
                       Compact_cai(analysis::CategoryName_cai(group.category)) +
                       ", \"count\": " + std::to_string(group.count) + ", \"items\": [";
                for (std::size_t i = 0; i < group.items.size(); ++i) {
                    const analysis::Control& control = group.items[i];
                    Json item = Json::object();
                    item["share"] = control.share ? Json(*control.share) : Json(nullptr);
                    item["refs"]  = control.refs;
                    item["tip"]   = control.tip;
                    item["file"]  = control.file;
                    out += (i == 0 ? "\n      " : ",\n      ") + Compact_cai(item);
                }
                out += group.items.empty() ? "] }" : "\n    ] }";
            }
            out += data.controls.empty() ? "}\n" : "\n  }\n";
            out += "}\n";
            return out;
        } catch (...) {
            return {};
        }
    }
};

} // namespace

// The store the plugin uses (JSON).
std::unique_ptr<AnalysisStore> MakeAnalysisStore_cai()
{
    return std::make_unique<JsonAnalysisStore>();
}

} // namespace sc::store
