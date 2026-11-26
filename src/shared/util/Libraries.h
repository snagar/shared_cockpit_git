// shared_cockpit - the third-party libraries in the plugin (Home > About, 0.9.5)
//
// The ones sc_core is built with, with the versions from their own headers. The plugin adds
// those it builds itself (Dear ImGui, ImgWindow, the X-Plane SDK) in HomePage.

#pragma once

#include <string>
#include <vector>

namespace sc::util {

struct Library {
    std::string name;
    std::string version;       // empty when the library has none
    std::string license;
    std::string use;           // what we use it for
};

// fmt, loguru, ENet, nlohmann/json, Font Awesome.
std::vector<Library> CoreLibraries_cai();

} // namespace sc::util
