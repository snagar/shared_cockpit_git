// shared_cockpit - shared helper functions with no dependencies

#include "shared/util/Utils.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <charconv>
#include <locale>
#include <sstream>
#include <ctime>
#include <fstream>
#include <iterator>
#include <system_error>

namespace sc {

namespace fs = std::filesystem;

// A path as UTF-8 text.
std::string Utils::Utf8_cai(const fs::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

// UTF-8 text as a path.
fs::path Utils::PathFromUtf8_cai(std::string_view text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

// The whole file as bytes; empty when it cannot be read.
std::string Utils::ReadAll_cai(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Writes `text` through a temporary file + rename, so `path` is never half written.
bool Utils::WriteFileSafely_cai(const fs::path& path, const std::string& text, std::string& error)
{
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot write " + Utf8_cai(tmp);
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            error = "write failed: " + Utf8_cai(tmp);
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        error = "cannot replace " + Utf8_cai(path) + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// Copies `from` to `to` through a temporary file + rename.
bool Utils::CopyFileSafely_cai(const fs::path& from, const fs::path& to, std::string& error)
{
    fs::path tmp = to;
    tmp += ".tmp";
    std::error_code ec;
    fs::copy_file(from, tmp, fs::copy_options::overwrite_existing, ec);
    if (!ec)
        fs::rename(tmp, to, ec);
    if (ec) {
        error = "cannot copy " + Utf8_cai(from) + " to " + Utf8_cai(to) + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// A file's modification time as a system_clock time: both clocks' "now" are
// measured, which is exact to well under a second.
std::chrono::system_clock::time_point Utils::ToSystemTime_cai(fs::file_time_type fileTime)
{
    using namespace std::chrono;
    return time_point_cast<system_clock::duration>(fileTime - fs::file_time_type::clock::now() + system_clock::now());
}

// "<size>;<seconds since 1970>" of a file; empty when missing.
std::string Utils::FileStamp_cai(const fs::path& path)
{
    std::error_code sizeEc, timeEc;
    const auto size = fs::file_size(path, sizeEc);
    const auto time = fs::last_write_time(path, timeEc);
    if (sizeEc || timeEc)
        return {};
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(ToSystemTime_cai(time).time_since_epoch());
    return std::to_string(size) + ";" + std::to_string(seconds.count());
}

// `text` without spaces, tabs and CR at both ends.
std::string_view Utils::Trim_cai(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

// `text` in lower case (ASCII letters only).
std::string Utils::Lower_cai(std::string_view text)
{
    std::string lower(text);
    for (char& character : lower)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return lower;
}

// Words split at spaces and tabs.
std::vector<std::string> Utils::SplitWords_cai(std::string_view text)
{
    std::vector<std::string> words;
    std::size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t'))
            ++at;
        std::size_t end = at;
        while (end < text.size() && text[end] != ' ' && text[end] != '\t')
            ++end;
        if (end > at)
            words.emplace_back(text.substr(at, end - at));
        at = end;
    }
    return words;
}

// Parts split at `separator`, each trimmed (empty parts kept).
std::vector<std::string> Utils::Split_cai(std::string_view text, char separator)
{
    std::vector<std::string> parts;
    std::size_t at = 0;
    while (true) {
        const std::size_t end = text.find(separator, at);
        parts.emplace_back(Trim_cai(text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at)));
        if (end == std::string_view::npos)
            break;
        at = end + 1;
    }
    return parts;
}

// True when all of `text` is a decimal number (the "C" locale: '.' as the decimal point).
bool Utils::ParseFloat_cai(std::string_view text, double& value)
{
    text = Trim_cai(text);
    if (text.empty())
        return false;
    std::istringstream number{std::string(text)};
    number.imbue(std::locale::classic());
    double parsed = 0.0;
    number >> parsed;
    if (!number || number.peek() != std::char_traits<char>::eof())
        return false;
    value = parsed;
    return true;
}

// The same, as a float.
bool Utils::ParseFloat_cai(std::string_view text, float& value)
{
    double parsed = 0.0;
    if (!ParseFloat_cai(text, parsed))
        return false;
    value = static_cast<float>(parsed);
    return true;
}

// True when all of `text` is a whole number.
bool Utils::ParseInt_cai(std::string_view text, int& value)
{
    text = Trim_cai(text);
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && ec == std::errc() && ptr == text.data() + text.size();
}

// True when `text` contains `word`.
bool Utils::Contains_cai(std::string_view text, std::string_view word)
{
    return !word.empty() && text.find(word) != std::string_view::npos;
}

// "1 control" / "3 controls".
std::string Utils::Plural_cai(std::size_t count, std::string_view one, std::string_view many)
{
    return std::to_string(count) + " " + std::string(count == 1 ? one : many);
}

// Local time now as "2026-11-22T19:41:00".
std::string Utils::NowIso_cai()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32] = {};
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &local);
    return text;
}

// `text` made safe as a file name.
std::string Utils::SafeFileName_cai(std::string_view text)
{
    std::string safe;
    for (char character : text) {
        const unsigned char code = static_cast<unsigned char>(character);
        safe += (std::isalnum(code) || character == '-' || character == '_' || character == '.') ? character : '_';
    }
    return safe;
}

// A 64-bit FNV-1a hash of `text` as 16 hex digits.
std::string Utils::Hash_cai(std::string_view text)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (char character : text) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 1099511628211ull;
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = kHex[hash & 0xF];
        hash >>= 4;
    }
    return out;
}

// The great-circle (haversine) distance in metres.
double Utils::DistanceM_cai(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double kEarthRadiusM = 6371008.8;
    constexpr double kRad = 3.14159265358979323846 / 180.0;
    const double dLat = (lat2 - lat1) * kRad;
    const double dLon = (lon2 - lon1) * kRad;
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
                     std::cos(lat1 * kRad) * std::cos(lat2 * kRad) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2.0 * kEarthRadiusM * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

// The smallest difference of two headings (0..180 degrees).
double Utils::HeadingDiff_cai(double a, double b)
{
    const double d = std::fmod(std::fabs(a - b), 360.0);
    return d > 180.0 ? 360.0 - d : d;
}

// A distance for the screens.
std::string Utils::DistanceText_cai(double metres)
{
    char text[32];
    if (metres < 10.0)
        std::snprintf(text, sizeof(text), "%.1f m", metres);
    else if (metres < 1000.0)
        std::snprintf(text, sizeof(text), "%.0f m", metres);
    else
        std::snprintf(text, sizeof(text), "%.1f km", metres / 1000.0);
    return text;
}

} // namespace sc
