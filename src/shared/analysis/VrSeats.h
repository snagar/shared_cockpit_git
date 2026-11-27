// shared_cockpit - an aircraft's seats: the teleport hotspots of its vrconfig file (0.9.6)
//
// <aircraft folder>/<acf name>_vrconfig.txt is X-Plane's standard VR configuration. Each
//   BEGIN_TELEPORT_HOTSPOT SITTING|STANDING <name>
//     PRESET_XYZ <x> <y> <z>        aircraft coordinates, metres (x right, y up, z aft)
//     PRESET_PSI / _THE / _PHI <deg> heading / pitch / roll
//   END_TELEPORT_HOTSPOT
// is a seat. Nothing else in the file is read. New seats: made with the vrconfig_helper plugin.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace sc::analysis {

struct VrSeat {
    std::string name;
    bool  sitting = true;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float psi = 0.0f, the = 0.0f, phi = 0.0f;
};

// <acf folder>/<acf stem>_vrconfig.txt.
std::filesystem::path VrConfigPath_cai(const std::filesystem::path& acfFile);

// The seats in `acfFile`'s vrconfig file, in file order; empty when it has none or no file.
std::vector<VrSeat> ReadVrSeats_cai(const std::filesystem::path& acfFile);

} // namespace sc::analysis
