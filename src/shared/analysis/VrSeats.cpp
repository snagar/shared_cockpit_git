// shared_cockpit - an aircraft's seats from its vrconfig file (0.9.6)
#include "shared/analysis/VrSeats.h"

#include <fstream>

#include "shared/log/Log.h"
#include "shared/util/Utils.h"

namespace sc::analysis {

namespace fs = std::filesystem;

// <acf folder>/<acf stem>_vrconfig.txt.
fs::path VrConfigPath_cai(const fs::path& acfFile)
{
    return acfFile.parent_path() / (acfFile.stem().native() + fs::path("_vrconfig.txt").native());
}

// The teleport hotspots of the vrconfig file.
std::vector<VrSeat> ReadVrSeats_cai(const fs::path& acfFile)
{
    std::vector<VrSeat> seats;
    const fs::path path = VrConfigPath_cai(acfFile);
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return seats;
    auto lmbda_float = [](const std::vector<std::string>& words, std::size_t at) {
        float value = 0.0f;
        if (at < words.size() && !Utils::ParseFloat_cai(words[at], value))
            value = 0.0f;
        return value;
    };
    bool inside = false;
    VrSeat seat;
    std::string line;
    while (std::getline(in, line)) {
        const std::string_view text = Utils::Trim_cai(line);
        if (text.empty() || text.front() == '#')
            continue;
        const std::vector<std::string> words = Utils::SplitWords_cai(text);
        if (words.empty())
            continue;
        if (words[0] == "BEGIN_TELEPORT_HOTSPOT") {
            inside = true;
            seat = VrSeat{};
            seat.sitting = words.size() < 2 || words[1] != "STANDING";
            // The name: the rest of the line after the type.
            const std::size_t typeAt = words.size() >= 2 ? text.find(words[1]) : std::string_view::npos;
            if (typeAt != std::string_view::npos)
                seat.name = std::string(Utils::Trim_cai(text.substr(typeAt + words[1].size())));
            if (seat.name.empty())
                seat.name = "Seat " + std::to_string(seats.size() + 1);
        } else if (!inside) {
            continue;
        } else if (words[0] == "PRESET_XYZ") {
            seat.x = lmbda_float(words, 1);
            seat.y = lmbda_float(words, 2);
            seat.z = lmbda_float(words, 3);
        } else if (words[0] == "PRESET_PSI") {
            seat.psi = lmbda_float(words, 1);
        } else if (words[0] == "PRESET_THE") {
            seat.the = lmbda_float(words, 1);
        } else if (words[0] == "PRESET_PHI") {
            seat.phi = lmbda_float(words, 1);
        } else if (words[0] == "END_TELEPORT_HOTSPOT") {
            seats.push_back(seat);
            inside = false;
        }
    }
    LOG_INFO("seats: {} in {}", seats.size(), Utils::Utf8_cai(path.filename()));
    return seats;
}

} // namespace sc::analysis
