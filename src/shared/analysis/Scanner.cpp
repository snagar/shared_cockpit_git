// shared_cockpit - reads an aircraft's files in a background thread

#include "shared/analysis/Scanner.h"

#include <utility>

namespace sc::analysis {

Scanner::~Scanner()
{
    Stop_cai();
}

// Requests the thread to stop and waits for it.
void Scanner::Stop_cai()
{
    if (mThread.Joinable_cai()) {
        mThread.RequestStop_cai();
        mThread.Join_cai();
    }
    std::lock_guard lock(mMutex);
    mRunning = false;
}

// Starts reading `acfFile` and its objects in the thread.
void Scanner::Start_cai(const std::filesystem::path& acfFile, const ManipTypeSet& types)
{
    Stop_cai();
    {
        std::lock_guard lock(mMutex);
        mResult.reset();
        mRunning = true;
    }
    mProgress.filesTotal = 0;
    mProgress.filesDone  = 0;
    mProgress.bytesTotal = 0;
    mProgress.bytesDone  = 0;
    // Everything the thread uses is copied into it.
    auto lmbda_scan = [this, acfFile, types](StopToken stop) {
        AircraftScan result = ScanAircraft_cai(acfFile, types, stop, &mProgress);
        std::lock_guard lock(mMutex);
        if (!result.aborted && !stop.StopRequested_cai())
            mResult = std::move(result);
        mRunning = false;
    };
    mThread = StopThread(lmbda_scan);
}

// Stops the running scan; its result is discarded.
void Scanner::Abort_cai()
{
    Stop_cai();
    std::lock_guard lock(mMutex);
    mResult.reset();
}

// True while the thread is reading.
bool Scanner::Running_cai() const
{
    std::lock_guard lock(mMutex);
    return mRunning;
}

// 0..1 by bytes read.
float Scanner::Fraction_cai() const
{
    const auto total = mProgress.bytesTotal.load();
    if (total == 0)
        return 0.0f;
    return static_cast<float>(static_cast<double>(mProgress.bytesDone.load()) / static_cast<double>(total));
}

// The finished scan's result, once.
std::optional<AircraftScan> Scanner::TakeResult_cai()
{
    std::lock_guard lock(mMutex);
    if (mRunning || !mResult)
        return std::nullopt;
    std::optional<AircraftScan> result = std::move(mResult);
    mResult.reset();
    return result;
}

} // namespace sc::analysis
