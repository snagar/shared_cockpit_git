// shared_cockpit - reads an aircraft's .acf and OBJ files (ATTR_manip_ lines)

#include "shared/analysis/ObjScan.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <set>
#include <system_error>

#include "fmt/format.h"

#include "shared/util/Utils.h"

namespace sc::analysis {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kManipPrefix = "ATTR_manip_";
constexpr std::size_t      kMaxProblems = 20;        // more are counted, not listed
constexpr std::size_t      kCheckEvery  = 4096;      // lines between stop checks / progress updates

// True for a space, tab or CR.
bool IsSpace_cai(char character) { return character == ' ' || character == '\t' || character == '\r'; }

// The next word of `rest` (which moves past it).
std::string_view NextToken_cai(std::string_view& rest)
{
    std::size_t first = 0;
    while (first < rest.size() && IsSpace_cai(rest[first]))
        ++first;
    std::size_t last = first;
    while (last < rest.size() && !IsSpace_cai(rest[last]))
        ++last;
    const std::string_view token = rest.substr(first, last - first);
    rest = rest.substr(last);
    return token;
}

// State shared by the files of one scan: controls merged across files.
struct Merger {
    AircraftScan&                      scan;
    std::map<std::string, std::size_t> byKey;     // control key -> index in scan.controls
};

// Reads one OBJ file's ATTR_manip_ lines into the scan. `bytesBefore` is the
// progress of the files before it. Returns false when stopped.
bool ScanOneFile_cai(const fs::path& path, const ManipTypeSet& types, Merger& merger, StopToken stop,
                     ScanProgress* progress, std::uintmax_t& bytesBefore, ScannedFile& file)
{
    AircraftScan& scan = merger.scan;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        scan.problems.push_back(fmt::format("cannot read {}", Utils::Utf8_cai(path.filename())));
        return true;
    }
    const std::string fileName = Utils::Utf8_cai(path.filename());
    std::size_t problemCount = 0;
    std::string line;
    std::size_t lineNumber = 0;
    std::uintmax_t bytesInFile = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        bytesInFile += line.size() + 1;
        if (lineNumber % kCheckEvery == 0) {
            if (stop.StopRequested_cai())
                return false;
            if (progress)
                progress->bytesDone = bytesBefore + bytesInFile;
        }
        std::string_view rest = line;
        const std::string_view keyword = NextToken_cai(rest);
        if (!keyword.starts_with(kManipPrefix))
            continue;
        const std::string type(keyword.substr(kManipPrefix.size()));
        const ManipTypeDef* def = types.Find_cai(type);
        if (!def) {
            if (types.ignored.contains(type))
                ++scan.notUsed[type];
            else
                ++scan.unsupported[type];
            continue;
        }
        std::vector<std::string_view> params;
        for (int param = 0; param < def->params; ++param) {
            const std::string_view token = NextToken_cai(rest);
            if (token.empty())
                break;
            params.push_back(token);
        }
        if (static_cast<int>(params.size()) < def->params) {
            if (++problemCount <= kMaxProblems)
                scan.problems.push_back(fmt::format("{} line {}: {} needs {} values", fileName, lineNumber, keyword,
                                                    def->params));
            continue;
        }
        ++file.manipulators;

        FoundControl control;
        control.type     = def->type;
        control.category = def->category;
        for (int ref : def->refs) {
            const std::string_view value = params[static_cast<std::size_t>(ref)];
            if (value != "none")                       // OBJ files write "none" for "no dataref"
                control.refs.emplace_back(value);
        }
        control.tooltip = std::string(Utils::Trim_cai(rest));
        // Commands and datarefs always have a '/'; anything else is not a control
        // ("ANIM_keyframe_loop" in an animation). Device names have none.
        auto lmbda_noSlash = [](const std::string& ref) { return ref.find('/') == std::string::npos; };
        if (control.refs.empty() ||
            (def->category != Category::CantSync && std::any_of(control.refs.begin(), control.refs.end(), lmbda_noSlash))) {
            if (++problemCount <= kMaxProblems)
                scan.problems.push_back(fmt::format("{} line {}: {} without a command or dataref - skipped", fileName,
                                                    lineNumber, keyword));
            continue;
        }

        const std::string key = ControlKey_cai(control.type, control.refs);
        if (const auto known = merger.byKey.find(key); known != merger.byKey.end()) {
            FoundControl& same = scan.controls[known->second];
            ++same.clickAreas;
            if (same.tooltip.empty())
                same.tooltip = control.tooltip;
            continue;
        }
        control.objFile    = fileName;
        control.clickAreas = 1;
        merger.byKey.emplace(key, scan.controls.size());
        scan.controls.push_back(std::move(control));
    }
    if (problemCount > kMaxProblems)
        scan.problems.push_back(fmt::format("{}: ... and {} more", fileName, problemCount - kMaxProblems));
    bytesBefore += bytesInFile;
    return true;
}

// A path in lower case with '/' separators (to find duplicates).
std::string LowerPath_cai(const fs::path& path)
{
    return Utils::Lower_cai(path.generic_string());
}

} // namespace

// The objects listed in the .acf ("P _obja/<n>/_v10_att_file_stl <path>").
std::vector<AcfObject> ReadAcfObjects_cai(const fs::path& acfFile, std::string& error)
{
    std::vector<AcfObject> objects;
    std::ifstream in(acfFile, std::ios::binary);
    if (!in) {
        error = fmt::format("cannot read {}", Utils::Utf8_cai(acfFile));
        return objects;
    }
    constexpr std::string_view kPrefix = "P _obja/";
    constexpr std::string_view kKey    = "/_v10_att_file_stl";
    const fs::path objectsFolder = acfFile.parent_path() / "objects";

    std::vector<std::pair<int, std::string>> listed;
    std::string line;
    while (std::getline(in, line)) {
        std::string_view view = line;
        if (!view.starts_with(kPrefix))
            continue;
        view.remove_prefix(kPrefix.size());
        const std::size_t keyAt = view.find(kKey);
        if (keyAt == std::string_view::npos)
            continue;
        int index = 0;
        const auto [ptr, ec] = std::from_chars(view.data(), view.data() + keyAt, index);
        if (ec != std::errc() || ptr != view.data() + keyAt)
            continue;
        const std::string_view value = Utils::Trim_cai(view.substr(keyAt + kKey.size()));
        if (!value.empty())
            listed.emplace_back(index, std::string(value));
    }
    auto lmbda_byIndex = [](const auto& a, const auto& b) { return a.first < b.first; };
    std::stable_sort(listed.begin(), listed.end(), lmbda_byIndex);

    std::set<std::string> seen;
    for (const auto& [index, written] : listed) {
        std::string slashes = written;
        std::replace(slashes.begin(), slashes.end(), '\\', '/');
        AcfObject object;
        object.path     = (objectsFolder / Utils::PathFromUtf8_cai(slashes)).lexically_normal();
        object.listedAs = written;
        std::error_code ec;
        object.exists   = fs::is_regular_file(object.path, ec);
        if (seen.insert(LowerPath_cai(object.path)).second)
            objects.push_back(std::move(object));
    }

    AcfObject cockpit;
    cockpit.path     = (acfFile.parent_path() / (acfFile.stem().u8string() + u8"_cockpit.obj")).lexically_normal();
    cockpit.listedAs = Utils::Utf8_cai(cockpit.path.filename());
    cockpit.byName   = true;
    std::error_code ec;
    cockpit.exists   = fs::is_regular_file(cockpit.path, ec);
    if (cockpit.exists && !seen.contains(LowerPath_cai(cockpit.path)))
        objects.insert(objects.begin(), std::move(cockpit));
    return objects;
}

// The plugin folders inside <aircraft folder>/plugins.
std::vector<std::string> FindAircraftPlugins_cai(const fs::path& aircraftFolder)
{
    std::vector<std::string> plugins;
    std::error_code ec;
    const fs::path folder = aircraftFolder / "plugins";
    if (!fs::is_directory(folder, ec))
        return plugins;
    for (const auto& entry : fs::directory_iterator(folder, fs::directory_options::skip_permission_denied, ec)) {
        std::error_code entryEc;
        if (entry.is_directory(entryEc))
            plugins.push_back(Utils::Utf8_cai(entry.path().filename()));
    }
    std::sort(plugins.begin(), plugins.end());
    return plugins;
}

// Reads the .acf object list, then every listed OBJ file.
AircraftScan ScanAircraft_cai(const fs::path& acfFile, const ManipTypeSet& types, StopToken stop,
                              ScanProgress* progress)
{
    AircraftScan scan;
    scan.acfFile  = acfFile;
    scan.acfStamp = Utils::FileStamp_cai(acfFile);
    const std::vector<AcfObject> objects = ReadAcfObjects_cai(acfFile, scan.error);
    scan.pluginsFound = FindAircraftPlugins_cai(acfFile.parent_path());
    if (progress) {
        std::uintmax_t total = 0;
        for (const AcfObject& object : objects) {
            std::error_code ec;
            if (object.exists)
                total += fs::file_size(object.path, ec);
        }
        progress->filesTotal = objects.size();
        progress->filesDone  = 0;
        progress->bytesTotal = total;
        progress->bytesDone  = 0;
    }
    Merger merger{scan, {}};
    std::uintmax_t bytesBefore = 0;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (stop.StopRequested_cai()) {
            scan.aborted = true;
            return scan;
        }
        ScannedFile file;
        file.path   = objects[i].path;
        file.exists = objects[i].exists;
        if (file.exists) {
            file.stamp = Utils::FileStamp_cai(file.path);
            if (!ScanOneFile_cai(file.path, types, merger, stop, progress, bytesBefore, file)) {
                scan.aborted = true;
                return scan;
            }
        } else {
            scan.problems.push_back(fmt::format("missing object: {}", objects[i].listedAs));
        }
        scan.files.push_back(std::move(file));
        if (progress) {
            progress->bytesDone = bytesBefore;
            progress->filesDone = i + 1;
        }
    }
    return scan;
}

// Reads the given OBJ files only.
AircraftScan ScanObjFiles_cai(const std::vector<fs::path>& files, const ManipTypeSet& types)
{
    AircraftScan scan;
    Merger merger{scan, {}};
    std::uintmax_t bytesBefore = 0;
    for (const fs::path& path : files) {
        ScannedFile file;
        file.path = path;
        std::error_code ec;
        file.exists = fs::is_regular_file(path, ec);
        if (file.exists) {
            file.stamp = Utils::FileStamp_cai(path);
            ScanOneFile_cai(path, types, merger, {}, nullptr, bytesBefore, file);
        }
        scan.files.push_back(std::move(file));
    }
    return scan;
}

} // namespace sc::analysis
