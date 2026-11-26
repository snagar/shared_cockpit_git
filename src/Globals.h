// shared_cockpit - runtime values shared by the whole plugin
//
// One object, filled once in XPluginStart, read everywhere through
// sc::Globals::get_cai(). Only plain values here: no X-Plane calls, so any
// file (and the test tools) can include it.

#pragma once

#include <filesystem>
#include <string>

namespace sc {

class Globals {
public:
    // The one instance of the shared values.
    static Globals& get_cai();

    std::filesystem::path pluginFolder;     // .../Resources/plugins/shared_cockpit/
    std::filesystem::path xplaneFolder;     // X-Plane's root folder
    std::filesystem::path aircraftFolder;   // <X-Plane>/Aircraft
    std::string           xplaneVersion;    // e.g. "12.40"
    bool                  inVr = false;     // X-Plane is in VR now

    // <plugin folder>/backups/
    std::filesystem::path backupFolder_cai() const;

private:
    Globals() = default;
};

} // namespace sc
