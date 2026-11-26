// shared_cockpit - logging
//
// Thin layer over loguru (taken from vrconfig_helper, now formatted with fmt):
//   * every message goes to "<plugin folder>/shared_cockpit.log"
//     (rotated: .log -> .log.1 -> .log.2 at start-up and when the file grows past
//     info::kLogMaxFileSize; the last 2 logs are kept)
//   * optionally mirrored to X-Plane's Log.txt
//
// Use the macros, they add the calling function automatically:
//     LOG_INFO("plugin started, version {}", version);  ->  "[XPluginStart] plugin started, version 0.1"
//     LOG_DEBUG("value={}", v);                        ->  "[Load_cai:42] value=7"
//
// __func__ is "operator()" inside a lambda, so lambdas are named "lmbda_<name>"
// and log through the *_AS macros with that name:
//     auto lmbda_onClick = [&]() { LOG_INFO_AS("lmbda_onClick", "clicked {}", id); };

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include "fmt/format.h"

namespace sc::log {

enum class Level : int {
    Error   = -2,
    Warning = -1,
    Info    =  0,
    Debug   =  1,
};

// Starts logging into <directory>/<pluginName>.log. Safe to call more than once.
bool Init_cai(std::string_view pluginName, const std::filesystem::path& directory);
// Flushes and closes the log file.
void Shutdown_cai();

// Where mirrored lines go: the plugin passes XPLMDebugString (X-Plane's Log.txt), the
// command-line tools a console printer. No sink = no mirror. This keeps Log free of
// X-Plane, so the tools and the network code log exactly like the plugin.
using MirrorSink = void (*)(const char* line);
// mainThreadOnly: only the calling thread calls the sink; other threads' lines wait for FlushMirror_cai.
void SetMirrorSink_cai(MirrorSink sink, bool mainThreadOnly = false);
// Writes the lines other threads left for the mirror (call it on the main thread, e.g. every flight loop).
void FlushMirror_cai();

// Also writes every message to the mirror (default: info::kLogToXPlaneDefault).
void SetMirrorToXPlane_cai(bool enabled);
// Only messages of `level` and more important are mirrored (the plugin: Warning - errors and
// warnings in Log.txt, everything in the plugin log; the tools: Debug - all on the console).
void SetMirrorLevel_cai(Level level);
// One line straight to the mirror, not to the plugin log (the plugin's start and stop lines).
void Mirror_cai(std::string_view line);
// Messages above this level are dropped (Debug in debug builds, Info otherwise).
void SetLevel_cai(Level level);
// The build's level: Debug in a Debug build, Info in a Release build ([debug] log_level=default).
Level DefaultLevel_cai();
// True when messages of `level` are written.
bool IsEnabled_cai(Level level);
// The log file in use.
std::filesystem::path GetLogFilePath_cai();

// Writes an already formatted message.
void Write_cai(Level level, const char* file, unsigned line, std::string_view message);

// Formats and writes a message, prefixed with the calling function.
template <class... Args>
void Format_cai(Level level, const char* file, unsigned line, const char* func,
                fmt::format_string<Args...> format, Args&&... args)
{
    if (!IsEnabled_cai(level))
        return;
    const std::string body = fmt::format(format, std::forward<Args>(args)...);
    if (level == Level::Debug)
        Write_cai(level, file, line, fmt::format("[{}:{}] {}", func, line, body));
    else
        Write_cai(level, file, line, fmt::format("[{}] {}", func, body));
}

} // namespace sc::log

// --- Logging macros ------------------------------------------------------------------
#define SC_LOG_IMPL(level, func, format, ...) \
    ::sc::log::Format_cai((level), __FILE__, __LINE__, (func), format __VA_OPT__(,) __VA_ARGS__)

#define LOG_ERROR(format, ...) SC_LOG_IMPL(::sc::log::Level::Error,   __func__, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN(format, ...)  SC_LOG_IMPL(::sc::log::Level::Warning, __func__, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO(format, ...)  SC_LOG_IMPL(::sc::log::Level::Info,    __func__, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_DEBUG(format, ...) SC_LOG_IMPL(::sc::log::Level::Debug,   __func__, format __VA_OPT__(,) __VA_ARGS__)

// For lambdas: pass the lambda's name ("lmbda_...") instead of __func__.
#define LOG_ERROR_AS(name, format, ...) SC_LOG_IMPL(::sc::log::Level::Error,   name, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_WARN_AS(name, format, ...)  SC_LOG_IMPL(::sc::log::Level::Warning, name, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_INFO_AS(name, format, ...)  SC_LOG_IMPL(::sc::log::Level::Info,    name, format __VA_OPT__(,) __VA_ARGS__)
#define LOG_DEBUG_AS(name, format, ...) SC_LOG_IMPL(::sc::log::Level::Debug,   name, format __VA_OPT__(,) __VA_ARGS__)
