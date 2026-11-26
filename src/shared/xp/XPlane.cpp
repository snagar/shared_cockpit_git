// shared_cockpit - X-Plane side helpers: folders and the user aircraft

#include "shared/xp/XPlane.h"

#include "XPLMDataAccess.h"
#include "XPLMPlanes.h"
#include "XPLMPlugin.h"
#include "XPLMUtilities.h"

#include "fmt/format.h"

#include "shared/log/Log.h"
#include "shared/util/Utils.h"

namespace sc::xp {

namespace fs = std::filesystem;

// The user's aircraft (index 0) with its display name.
std::optional<UserAircraft> GetUserAircraft_cai()
{
    char fileName[256] = {};
    char path[1024]    = {};
    XPLMGetNthAircraftModel(0, fileName, path);
    if (path[0] == '\0') {
        LOG_WARN("X-Plane returned no path for the user aircraft");
        return std::nullopt;
    }
    UserAircraft aircraft;
    aircraft.acfFile = Utils::PathFromUtf8_cai(path);

    static XPLMDataRef uiName = XPLMFindDataRef("sim/aircraft/view/acf_ui_name");
    char ui[251] = {};
    if (uiName)
        XPLMGetDatab(uiName, ui, 0, 250);
    aircraft.displayName = ui[0] ? std::string(ui) : Utils::Utf8_cai(aircraft.acfFile.stem());
    return aircraft;
}

// X-Plane's root folder.
fs::path GetXPlaneFolder_cai()
{
    char root[2048] = {};
    XPLMGetSystemPath(root);                          // native path, ends with a separator
    return Utils::PathFromUtf8_cai(root);
}

// The plugin's own folder (the parent of the <platform>_x64 folder).
fs::path GetPluginFolder_cai()
{
    char path[2048] = {};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, path, nullptr, nullptr);
    fs::path dir = Utils::PathFromUtf8_cai(path).parent_path();
    const std::string sub = Utils::Utf8_cai(dir.filename());
    if (sub.size() > 4 && sub.compare(sub.size() - 4, 4, "_x64") == 0)
        dir = dir.parent_path();
    return dir;
}

// X-Plane's version as text, e.g. "12.40".
std::string GetXPlaneVersion_cai()
{
    int xplane = 0, xplm = 0;
    XPLMHostApplicationID host = 0;
    XPLMGetVersions(&xplane, &xplm, &host);
    return fmt::format("{}.{:02}", xplane / 1000, (xplane / 10) % 100);
}

} // namespace sc::xp
