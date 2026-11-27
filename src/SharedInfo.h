// shared_cockpit - fixed information about the plugin, shared by all its files
//
// The one place to change the version, the names, the About texts, the log
// limits, the window sizes and the names of the files the plugin writes.
// Runtime values (folders found at start-up, ...) live in Globals.h.
//
// All texts are string literals, so .data() is a valid C string for the X-Plane API.

#pragma once

#include <cstdint>
#include <string_view>

namespace sc::info {

// --- The plugin --------------------------------------------------------------------
inline constexpr std::string_view kVersion      = "0.9";     // change before a release
inline constexpr std::string_view kBuild        = "10";       // iteration, the zip is shared_cockpit_<version>.<build>.zip
inline constexpr std::string_view kPluginId     = "shared_cockpit";   // must match project() in CMakeLists.txt
inline constexpr std::string_view kPluginName   = "Shared Cockpit";
inline constexpr std::string_view kPluginDesc   = "Shares one cockpit between two X-Plane simulators";
inline constexpr std::string_view kMenuToggle   = "Toggle Window";
inline constexpr std::string_view kCmdToggle    = "shared_cockpit/toggle_main_window";
inline constexpr std::string_view kCmdToggleDesc = "Shared Cockpit: show/hide the window";
inline constexpr std::string_view kMenuToggleDeck    = "Toggle Flight Deck (2D)";
inline constexpr std::string_view kCmdToggleDeck     = "shared_cockpit/toggle_flight_deck";
inline constexpr std::string_view kCmdToggleDeckDesc = "Shared Cockpit: show/hide the small flight deck window (2D)";

// --- About box -----------------------------------------------------------------------
inline constexpr std::string_view kDesignedBy   = "Snagar.Dev";
inline constexpr std::string_view kCodedBy      = "Claude.Ai";
inline constexpr std::string_view kLicense      = "Free";
inline constexpr std::string_view kIconCredit   = "Icons: Font Awesome Free 5 (fa-solid-900) by Fonticons, Inc. - "
                                                  "icons CC BY 4.0, font SIL OFL 1.1 (fontawesome.com).";

// --- Log file ------------------------------------------------------------------------
inline constexpr bool           kLogToXPlaneDefault = true;                 // mirror to X-Plane's Log.txt (the plugin: warnings and errors only)
inline constexpr std::uintmax_t kLogMaxFileSize     = 5u * 1024u * 1024u;   // a new file is started past this size
inline constexpr int            kLogKeptCopies      = 2;                    // <plugin>.log.1 .. .2

// --- Window ------------------------------------------------------------------------
inline constexpr int kWindowWidth     = 880;    // default size (2D and VR)
inline constexpr int kWindowHeight    = 560;
inline constexpr int kWindowMinWidth  = 640;    // the user may resize between these
inline constexpr int kWindowMinHeight = 480;
inline constexpr int kWindowMaxWidth  = 1600;
inline constexpr int kWindowMaxHeight = 1100;

// --- Files the plugin writes -----------------------------------------------------------
// The analysis of an aircraft: <aircraft folder>/<acf name>.shared_cockpit.<ext>, plus a copy
// in <plugin folder>/backups/ (aircraft installers may replace the aircraft folder).
// Since 0.3 (0.2 wrote sc_<acf name>.json: those files are not read any more).
inline constexpr std::string_view kAnalysisSuffix  = ".shared_cockpit";
inline constexpr std::string_view kBackupFolder    = "backups";
inline constexpr int              kAnalysisFormat  = 1;      // "format" in the file
// The manipulator types file shipped next to the plugin (custom_<name> replaces it).
inline constexpr std::string_view kTypesFile       = "sc_manip_types.ini";
inline constexpr int              kTypesFileVersion = 1;

} // namespace sc::info
