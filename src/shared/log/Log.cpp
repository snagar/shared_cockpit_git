// shared_cockpit - logging (loguru back-end)

#include "shared/log/Log.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "loguru.hpp"

#include "SharedInfo.h"
#include "shared/util/Utils.h"

namespace sc::log {

namespace fs = std::filesystem;

namespace {

constexpr char kFileSinkId[]   = "shared_cockpit_file";
constexpr char kXPlaneSinkId[] = "shared_cockpit_xplane";

struct State {
    std::mutex     mutex;              // guards file, path, size
    std::FILE*     file = nullptr;
    fs::path       path;
    std::uintmax_t size = 0;
    std::string    pluginName = std::string(info::kPluginId);
    bool           active = false;
};

// The logger's one state.
State& TheState_cai()
{
    static State state;
    return state;
}

std::atomic<bool> gMirrorToXPlane{info::kLogToXPlaneDefault};
std::atomic<int>  gMirrorLevel{static_cast<int>(Level::Debug)};   // mirrored: this level and more important
#ifdef NDEBUG
std::atomic<int>  gLevel{static_cast<int>(Level::Info)};
#else
std::atomic<int>  gLevel{static_cast<int>(Level::Debug)};
#endif

// The tag written before a message of `level` in X-Plane's Log.txt.
const char* LevelTag_cai(Level level)
{
    switch (level) {
        case Level::Error:   return "ERROR ";
        case Level::Warning: return "WARN ";
        case Level::Debug:   return "DEBUG ";
        default:             return "";
    }
}

std::atomic<MirrorSink> gMirrorSink{nullptr};
std::atomic<bool>       gMirrorMainOnly{false};   // the sink may only be called on the main thread (XPLMDebugString)
std::thread::id         gMainThread;              // the thread that set the sink
constexpr std::size_t   kMaxPending = 64;         // lines from other threads kept until FlushMirror_cai

// Lines written on other threads, waiting for the main thread.
struct Pending {
    std::mutex               mutex;
    std::vector<std::string> lines;
    std::size_t              dropped = 0;
};

// The one queue of pending lines.
Pending& ThePending_cai()
{
    static Pending pending;
    return pending;
}

// Writes one line to the mirror (X-Plane's Log.txt in the plugin, the console in the tools).
// Another thread's line waits for FlushMirror_cai: X-Plane's SDK may only be called on its main thread.
void XPlaneLine_cai(Level level, std::string_view message)
{
    const MirrorSink sink = gMirrorSink.load();
    if (!sink)
        return;
    std::string line = fmt::format("{}: {}{}\n", TheState_cai().pluginName, LevelTag_cai(level), message);
    if (gMirrorMainOnly.load() && std::this_thread::get_id() != gMainThread) {
        Pending& pending = ThePending_cai();
        std::lock_guard<std::mutex> lock(pending.mutex);
        if (pending.lines.size() < kMaxPending)
            pending.lines.push_back(std::move(line));
        else
            ++pending.dropped;
        return;
    }
    sink(line.c_str());
}

// <base>.<copyNumber>
fs::path Numbered_cai(const fs::path& base, int copyNumber)
{
    fs::path numbered = base;
    numbered += "." + std::to_string(copyNumber);
    return numbered;
}

// <plugin>.log.1 -> .2, <plugin>.log -> .1 (the oldest copy is dropped).
void RotateFiles_cai(const fs::path& base)
{
    std::error_code ec;
    fs::remove(Numbered_cai(base, info::kLogKeptCopies), ec);
    for (int i = info::kLogKeptCopies - 1; i >= 1; --i) {
        if (fs::exists(Numbered_cai(base, i), ec))
            fs::rename(Numbered_cai(base, i), Numbered_cai(base, i + 1), ec);
    }
    if (fs::exists(base, ec) && fs::file_size(base, ec) > 0)
        fs::rename(base, Numbered_cai(base, 1), ec);
}

// Opens the log file for writing. The caller holds TheState_cai().mutex.
bool OpenFile_cai()
{
    State& state = TheState_cai();
#if defined(_WIN32)
    state.file = _wfopen(state.path.c_str(), L"w");        // wide path: non-ASCII folders work
#else
    state.file = std::fopen(state.path.c_str(), "w");
#endif
    state.size = 0;
    return state.file != nullptr;
}

// loguru sink: appends a message to the log file, rotating it when too big.
void FileSink_cai(void*, const loguru::Message& message)
{
    State& state = TheState_cai();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.file)
        return;
    const std::string line = fmt::format("{}{}{}{}\n", message.preamble, message.indentation, message.prefix,
                                         message.message);
    std::fwrite(line.data(), 1, line.size(), state.file);
    std::fflush(state.file);                       // keep the log complete if X-Plane crashes
    state.size += line.size();
    if (state.size >= info::kLogMaxFileSize) {
        std::fclose(state.file);
        state.file = nullptr;
        RotateFiles_cai(state.path);
        OpenFile_cai();
    }
}

// loguru sink: flushes the log file.
void FileSinkFlush_cai(void*)
{
    State& state = TheState_cai();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.file)
        std::fflush(state.file);
}

// loguru sink: closes the log file.
void FileSinkClose_cai(void*)
{
    State& state = TheState_cai();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.file) {
        std::fclose(state.file);
        state.file = nullptr;
    }
}

// loguru: the name shown for the Debug level ("DBG"); nullptr = loguru's default.
const char* VerbosityName_cai(loguru::Verbosity verbosity)
{
    return verbosity == static_cast<loguru::Verbosity>(Level::Debug) ? "DBG" : nullptr;
}

// loguru sink: copies a message to X-Plane's Log.txt when mirroring is on.
void XPlaneSink_cai(void*, const loguru::Message& message)
{
    if (gMirrorToXPlane.load() && message.verbosity <= gMirrorLevel.load())
        XPlaneLine_cai(static_cast<Level>(message.verbosity), message.message);
}

} // namespace

// Starts logging into <directory>/<pluginName>.log.
bool Init_cai(std::string_view pluginName, const fs::path& directory)
{
    State& state = TheState_cai();
    if (state.active)
        Shutdown_cai();
    state.pluginName = std::string(pluginName);

    // loguru without loguru::init(): no signal handlers inside X-Plane, no argv parsing.
    loguru::g_stderr_verbosity = loguru::Verbosity_OFF;
    loguru::g_preamble_header  = false;
    loguru::g_preamble_date    = true;
    loguru::g_preamble_time    = true;
    loguru::g_preamble_uptime  = false;
    loguru::g_preamble_thread  = false;
    loguru::g_preamble_file    = true;
    loguru::g_preamble_verbose = true;
    loguru::g_preamble_pipe    = true;
    loguru::set_verbosity_to_name_callback(VerbosityName_cai);

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        std::error_code ec;
        fs::create_directories(directory, ec);
        state.path = directory / (state.pluginName + ".log");
        RotateFiles_cai(state.path);
        if (!OpenFile_cai()) {
            XPlaneLine_cai(Level::Error, fmt::format("[{}] cannot open log file {}", __func__, Utils::Utf8_cai(state.path)));
            return false;
        }
    }
    loguru::add_callback(kFileSinkId,   FileSink_cai,   nullptr, loguru::Verbosity_MAX, FileSinkClose_cai, FileSinkFlush_cai);
    loguru::add_callback(kXPlaneSinkId, XPlaneSink_cai, nullptr, loguru::Verbosity_MAX);
    state.active = true;
    return true;
}

// Flushes and closes the log file.
void Shutdown_cai()
{
    State& state = TheState_cai();
    if (!state.active)
        return;
    loguru::remove_callback(kXPlaneSinkId);
    loguru::remove_callback(kFileSinkId);     // calls FileSinkClose_cai
    state.active = false;
}

// Where mirrored lines go (XPLMDebugString in the plugin). With mainThreadOnly the calling thread is the
// only one that calls the sink; other threads' lines wait for FlushMirror_cai.
void SetMirrorSink_cai(MirrorSink sink, bool mainThreadOnly)
{
    gMainThread = std::this_thread::get_id();
    gMirrorMainOnly.store(mainThreadOnly);
    gMirrorSink.store(sink);
}

// Writes the lines other threads left for the mirror. Call it on the main thread.
void FlushMirror_cai()
{
    const MirrorSink sink = gMirrorSink.load();
    if (!sink || std::this_thread::get_id() != gMainThread)
        return;
    std::vector<std::string> lines;
    std::size_t dropped = 0;
    {
        Pending& pending = ThePending_cai();
        std::lock_guard<std::mutex> lock(pending.mutex);
        if (pending.lines.empty() && pending.dropped == 0)
            return;
        lines.swap(pending.lines);
        dropped = std::exchange(pending.dropped, 0);
    }
    for (const std::string& line : lines)
        sink(line.c_str());
    if (dropped > 0)
        sink(fmt::format("{}: WARN {} more lines were dropped, see the plugin log\n", TheState_cai().pluginName, dropped).c_str());
}
// Also writes every message to X-Plane's Log.txt.
void SetMirrorToXPlane_cai(bool enabled) { gMirrorToXPlane.store(enabled); }

// Only messages of this level and more important go to the mirror.
void SetMirrorLevel_cai(Level level) { gMirrorLevel.store(static_cast<int>(level)); }

// One line straight to the mirror (the plugin log gets it through the normal LOG_ macros).
void Mirror_cai(std::string_view line) { XPlaneLine_cai(Level::Info, line); }
// Messages above this level are dropped.
void SetLevel_cai(Level level)            { gLevel.store(static_cast<int>(level)); }
// True when messages of `level` are written.
bool IsEnabled_cai(Level level)           { return static_cast<int>(level) <= gLevel.load(); }

// The log file in use.
fs::path GetLogFilePath_cai()
{
    State& state = TheState_cai();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.path;
}

// Writes an already formatted message (to X-Plane's Log.txt before Init / after Shutdown).
void Write_cai(Level level, const char* file, unsigned line, std::string_view message)
{
    if (!TheState_cai().active) {
        XPlaneLine_cai(level, message);
        return;
    }
    const std::string text(message);
    loguru::log(static_cast<loguru::Verbosity>(level), file, line, "%s", text.c_str());
}

} // namespace sc::log
