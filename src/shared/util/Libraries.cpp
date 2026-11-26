// shared_cockpit - the third-party libraries in the plugin (0.9.5)
#include "shared/util/Libraries.h"

#include "enet/enet.h"
#include "fmt/format.h"
#include "loguru.hpp"
#include "nlohmann/json.hpp"

namespace sc::util {

// The libraries sc_core links, versions read from their headers.
std::vector<Library> CoreLibraries_cai()
{
    return {
        {"{fmt}", fmt::format("{}.{}.{}", FMT_VERSION / 10000, FMT_VERSION / 100 % 100, FMT_VERSION % 100), "MIT", "text formatting"},
        {"loguru", fmt::format("{}.{}.{}", LOGURU_VERSION_MAJOR, LOGURU_VERSION_MINOR, LOGURU_VERSION_PATCH), "Public domain (Unlicense)",
         "the plugin log"},
        {"ENet", fmt::format("{}.{}.{}", ENET_VERSION_MAJOR, ENET_VERSION_MINOR, ENET_VERSION_PATCH), "MIT", "the network (UDP)"},
        {"nlohmann/json",
         fmt::format("{}.{}.{}", NLOHMANN_JSON_VERSION_MAJOR, NLOHMANN_JSON_VERSION_MINOR, NLOHMANN_JSON_VERSION_PATCH), "MIT",
         "saved cockpit analyses"},
        {"Font Awesome Free", "5", "SIL OFL 1.1 (font)", "icons"},
    };
}

} // namespace sc::util
