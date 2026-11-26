// shared_cockpit - X-Plane side helpers: folders and the user aircraft
//
// Every function here calls the X-Plane SDK, so only the main (drawing / flight
// loop) thread may use them.

#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace sc::xp {

struct UserAircraft {
    std::filesystem::path acfFile;        // full path of the .acf
    std::string           displayName;    // acf_ui_name, or the .acf file name
};

// The user's aircraft (index 0). Empty when X-Plane reports no aircraft path.
std::optional<UserAircraft> GetUserAircraft_cai();

// X-Plane's root folder.
std::filesystem::path GetXPlaneFolder_cai();

// The plugin's own folder: .../Resources/plugins/shared_cockpit/ (the .xpl is in
// a <platform>_x64 sub-folder of it).
std::filesystem::path GetPluginFolder_cai();

// X-Plane's version as text, e.g. "12.40".
std::string GetXPlaneVersion_cai();

} // namespace sc::xp
