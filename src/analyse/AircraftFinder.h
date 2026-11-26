// shared_cockpit - finds every .acf below X-Plane's Aircraft folder
//
// Ported from vrconfig_helper. The search runs in a background thread (the
// folder can hold thousands of files) and only reads folder entries: no
// X-Plane, ImGui or log calls. Abort_cai() stops it; the destructor joins it.

#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>

#include "shared/util/StopThread.h"

namespace sc::analysis {

// Every .acf below `root`, sorted by path; liveries, plugins, objects (and similar)
// folders are skipped. Stops early (partial result) when `stop` is requested.
std::vector<std::filesystem::path> FindAllAcf_cai(const std::filesystem::path& root, StopToken stop = {},
                                                  std::atomic<std::size_t>* foldersSeen = nullptr);

class AircraftFinder {
public:
    AircraftFinder() = default;
    ~AircraftFinder();

    AircraftFinder(const AircraftFinder&)            = delete;
    AircraftFinder& operator=(const AircraftFinder&) = delete;

    // Starts searching below `root` (stops a running search first).
    void Start_cai(const std::filesystem::path& root);
    // Stops the search; its result is discarded.
    void Abort_cai();
    // True while searching.
    bool Running_cai() const;
    // Folders looked at so far.
    std::size_t FoldersSeen_cai() const { return mFoldersSeen; }
    // The finished search's result, once.
    std::optional<std::vector<std::filesystem::path>> TakeResult_cai();

private:
    // Requests the thread to stop and waits for it.
    void Stop_cai();

    StopThread                                        mThread;
    std::atomic<std::size_t>                          mFoldersSeen{0};
    mutable std::mutex                                mMutex;
    std::optional<std::vector<std::filesystem::path>> mResult;
    bool                                              mRunning = false;
};

} // namespace sc::analysis
