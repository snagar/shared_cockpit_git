// shared_cockpit - the manipulator types file (sc_manip_types.ini)
//
// Which OBJ manipulator types exist, where their commands / datarefs are, and
// the words that make a control start "kept local". The file ships next to the
// plugin; a user's custom_sc_manip_types.ini replaces it when present. Format:
// see the comments at the top of libs/sc_manip_types.ini.
//
// No X-Plane calls here (the scan thread uses the parsed set).

#pragma once

#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "shared/analysis/Analysis.h"

namespace sc::analysis {

struct ManipTypeDef {
    std::string      type;               // without "ATTR_manip_"
    int              params = 0;         // parameters before the tooltip (cursor included)
    std::vector<int> refs;               // 0-based indexes of the commands / datarefs among them
    Category         category = Category::Command;   // from kind: command, dataref, device = CantSync
};

struct ManipTypeSet {
    std::vector<ManipTypeDef> types;     // file order
    std::set<std::string>     ignored;   // counted as "not used"
    std::vector<std::string>  localWords;   // lower case
    int                       version = 0;
    std::vector<std::string>  problems;  // broken entries (skipped)

    // The definition of `type`, or nullptr.
    const ManipTypeDef* Find_cai(std::string_view type) const;
    // True when a control with these refs and tooltip should start kept local.
    bool LocalByDefault_cai(const std::vector<std::string>& refs, std::string_view tooltip) const;
};

// Parses the file's text. Never fails: broken entries are skipped and listed in `problems`.
ManipTypeSet ParseTypesIni_cai(std::string_view text);

// Reads custom_<name> when it exists in `folder`, else <name>. `usedFile` tells which
// one was read (empty when neither exists).
ManipTypeSet LoadTypesFile_cai(const std::filesystem::path& folder, std::filesystem::path& usedFile);

} // namespace sc::analysis
