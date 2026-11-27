// shared_cockpit - shared helper functions with no dependencies
//
// Only the C++ standard library: no X-Plane, ImGui, fmt or json, so every file
// and the test tools can use them. All functions are static members of Utils.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sc {

class Utils {
public:
    Utils() = delete;

    // --- paths and UTF-8 --------------------------------------------------------------
    // X-Plane, ImGui and the log use UTF-8. On Windows, path(std::string) and
    // .string() use the local code page instead, so always go through these two.

    // A path as UTF-8 text.
    static std::string Utf8_cai(const std::filesystem::path& path);
    // UTF-8 text as a path.
    static std::filesystem::path PathFromUtf8_cai(std::string_view text);

    // --- files ----------------------------------------------------------------------

    // The whole file as bytes; empty when it cannot be read.
    static std::string ReadAll_cai(const std::filesystem::path& path);
    // Writes `text` through a temporary file + rename, so `path` is never half written.
    static bool WriteFileSafely_cai(const std::filesystem::path& path, const std::string& text, std::string& error);
    // Copies `from` to `to` through a temporary file + rename.
    static bool CopyFileSafely_cai(const std::filesystem::path& from, const std::filesystem::path& to,
                                   std::string& error);
    // "<size>;<seconds since 1970>" of a file, to see later whether it changed; empty when missing.
    static std::string FileStamp_cai(const std::filesystem::path& path);
    // A file's modification time as a system_clock time (Apple's libc++ has no clock_cast).
    static std::chrono::system_clock::time_point ToSystemTime_cai(std::filesystem::file_time_type fileTime);

    // --- text -------------------------------------------------------------------------

    // `text` without spaces, tabs and CR at both ends.
    static std::string_view Trim_cai(std::string_view text);
    // `text` in lower case (ASCII letters only).
    static std::string Lower_cai(std::string_view text);
    // Words split at spaces and tabs.
    static std::vector<std::string> SplitWords_cai(std::string_view text);
    // Parts split at `separator`, each trimmed (empty parts kept).
    static std::vector<std::string> Split_cai(std::string_view text, char separator);
    // True when all of `text` is a whole number.
    static bool ParseInt_cai(std::string_view text, int& value);
    // True when all of `text` is a decimal number ("0.42", "-1e3"), always with a '.' whatever the
    // user's locale. Use it, not std::from_chars, for floats: macOS before 13.3 has no float from_chars.
    static bool ParseFloat_cai(std::string_view text, double& value);
    static bool ParseFloat_cai(std::string_view text, float& value);
    // True when `text` contains `word` (lower-case `text` expected).
    static bool Contains_cai(std::string_view text, std::string_view word);
    // "1 control" / "3 controls".
    static std::string Plural_cai(std::size_t count, std::string_view one, std::string_view many);
    // Local time now as "2026-11-22T19:41:00".
    static std::string NowIso_cai();
    // `text` made safe as a file name (letters, digits, '-', '_' and '.' kept, others become '_').
    static std::string SafeFileName_cai(std::string_view text);
    // A 64-bit FNV-1a hash of `text` as 16 hex digits (stable on every system).
    static std::string Hash_cai(std::string_view text);

    // --- Geography (WGS84 sphere) -----------------------------------------------------
    // The distance in metres between two latitude / longitude points (degrees).
    static double DistanceM_cai(double lat1, double lon1, double lat2, double lon2);
    // The smallest difference of two headings in degrees (0..180).
    static double HeadingDiff_cai(double a, double b);
    // "412 m", "3.2 km", "0.4 m" for the screens.
    static std::string DistanceText_cai(double metres);
};

} // namespace sc
