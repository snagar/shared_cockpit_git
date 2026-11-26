// shared_cockpit - reads an aircraft's files in a background thread
//
// Ported from vrconfig_helper's ObjScanner. The thread only reads files
// (ScanAircraft_cai): no X-Plane, ImGui or log calls. The page starts a scan,
// draws the progress each frame and takes the result when done (drawing thread).
// Abort_cai() stops it; the partial result is thrown away. The destructor stops
// and joins the thread.

#pragma once

#include <filesystem>
#include <mutex>
#include <optional>

#include "shared/analysis/ManipTypes.h"
#include "shared/analysis/ObjScan.h"
#include "shared/util/StopThread.h"

namespace sc::analysis {

class Scanner {
public:
    Scanner() = default;
    ~Scanner();

    Scanner(const Scanner&)            = delete;
    Scanner& operator=(const Scanner&) = delete;

    // Starts reading `acfFile` and its objects (stops a running scan first).
    void Start_cai(const std::filesystem::path& acfFile, const ManipTypeSet& types);
    // Stops the running scan; its result is discarded.
    void Abort_cai();
    // True while the thread is reading.
    bool Running_cai() const;
    // 0..1 by bytes read.
    float Fraction_cai() const;
    // Files read so far / in all.
    std::size_t FilesDone_cai() const  { return mProgress.filesDone; }
    std::size_t FilesTotal_cai() const { return mProgress.filesTotal; }
    // The finished scan's result, once (empty while running or when aborted).
    std::optional<AircraftScan> TakeResult_cai();

private:
    // Requests the thread to stop and waits for it.
    void Stop_cai();

    StopThread                  mThread;
    ScanProgress                mProgress;
    mutable std::mutex          mMutex;
    std::optional<AircraftScan> mResult;
    bool                        mRunning = false;
};

} // namespace sc::analysis
