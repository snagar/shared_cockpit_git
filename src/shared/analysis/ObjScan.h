// shared_cockpit - reads an aircraft's .acf and OBJ files (ATTR_manip_ lines)
//
// Ported from vrconfig_helper's ObjManips. A control is identified by its type
// and its commands / datarefs; its click areas (several ATTR_manip_ lines with
// the same identity, in one file or several) are merged into one control.
// Also lists the aircraft's own plugins (folders in <aircraft>/plugins).
//
// Only file reading: no X-Plane calls, no logging, so it runs in a background
// thread (Scanner). Messages are returned in `problems`.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "shared/analysis/ManipTypes.h"
#include "shared/util/StopThread.h"

namespace sc::analysis {

// One object listed in the .acf, or the cockpit object found by name.
struct AcfObject {
    std::filesystem::path path;           // resolved, normalized
    std::string           listedAs;       // as written in the .acf (or the file name)
    bool                  exists = false;
    bool                  byName = false; // <acf name>_cockpit.obj (X-Plane 10/11 do not list it)
};

// One control found (its click areas merged).
struct FoundControl {
    std::string              type;
    Category                 category = Category::Command;
    std::vector<std::string> refs;
    std::string              tooltip;     // first non-empty tooltip of its click areas
    std::string              objFile;     // first file it was found in (name only)
    std::size_t              clickAreas = 0;
};

// One file that was read.
struct ScannedFile {
    std::filesystem::path path;
    bool                  exists = false;
    std::string           stamp;          // Utils::FileStamp_cai
    std::size_t           manipulators = 0;   // ATTR_manip_ lines of defined types
};

// Progress of a scan, shared with the drawing thread.
struct ScanProgress {
    std::atomic<std::size_t>    filesTotal{0};
    std::atomic<std::size_t>    filesDone{0};
    std::atomic<std::uintmax_t> bytesTotal{0};
    std::atomic<std::uintmax_t> bytesDone{0};
};

// Everything read from one aircraft.
struct AircraftScan {
    std::filesystem::path              acfFile;
    std::string                        acfStamp;
    std::vector<ScannedFile>           files;        // .acf order
    std::vector<FoundControl>          controls;     // file / line order
    std::map<std::string, std::size_t> notUsed;      // ignored type -> click areas
    std::map<std::string, std::size_t> unsupported;  // unknown type -> lines
    std::vector<std::string>           pluginsFound; // plugin folder names in <aircraft>/plugins
    std::vector<std::string>           problems;
    std::string                        error;        // the .acf cannot be read
    bool                               aborted = false;
};

// The objects listed in `acfFile`, in .acf order (the cockpit object found by name
// first when it is not listed). Duplicates removed. Empty + `error` when unreadable.
std::vector<AcfObject> ReadAcfObjects_cai(const std::filesystem::path& acfFile, std::string& error);

// The plugin folders inside <aircraft folder>/plugins (sorted).
std::vector<std::string> FindAircraftPlugins_cai(const std::filesystem::path& aircraftFolder);

// Reads the .acf object list, then every listed OBJ file. Stops early (aborted)
// when `stop` is requested.
AircraftScan ScanAircraft_cai(const std::filesystem::path& acfFile, const ManipTypeSet& types,
                              StopToken stop = {}, ScanProgress* progress = nullptr);

// Reads the given OBJ files only (test tools; no .acf needed).
AircraftScan ScanObjFiles_cai(const std::vector<std::filesystem::path>& files, const ManipTypeSet& types);

} // namespace sc::analysis
