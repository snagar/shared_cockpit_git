// shared_cockpit - finds every .acf below X-Plane's Aircraft folder

#include "analyse/AircraftFinder.h"

#include <algorithm>
#include <string>
#include <system_error>

#include "shared/util/Utils.h"

namespace sc::analysis {

namespace fs = std::filesystem;

namespace {

constexpr int kMaxDepth = 8;               // Aircraft/<group>/<aircraft>/... is deep enough

// True for a file with the .acf extension.
bool IsAcf_cai(const fs::path& path)
{
    return Utils::Lower_cai(Utils::Utf8_cai(path.extension())) == ".acf";
}

// True for folders that never hold an aircraft's .acf.
bool SkippedFolder_cai(const fs::path& path)
{
    const std::string name = Utils::Lower_cai(Utils::Utf8_cai(path.filename()));
    return name == "liveries" || name == "plugins" || name == "objects" || name == "sounds" || name == "cockpit" ||
           name == "cockpit_3d" || name == "airfoils" || name == "weapons" || (!name.empty() && name.front() == '.');
}

} // namespace

// Every .acf below `root`, sorted by path.
std::vector<fs::path> FindAllAcf_cai(const fs::path& root, StopToken stop, std::atomic<std::size_t>* foldersSeen)
{
    std::vector<fs::path> files;
    std::error_code ec;
    const auto options = fs::directory_options::skip_permission_denied | fs::directory_options::follow_directory_symlink;
    fs::recursive_directory_iterator it(root, options, ec);
    for (const fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
        if (stop.StopRequested_cai())
            break;
        const fs::directory_entry& entry = *it;
        std::error_code entryError;
        if (entry.is_directory(entryError)) {
            if (foldersSeen)
                ++*foldersSeen;
            if (it.depth() >= kMaxDepth || SkippedFolder_cai(entry.path()))
                it.disable_recursion_pending();
            continue;
        }
        if (IsAcf_cai(entry.path()) && entry.is_regular_file(entryError))
            files.push_back(entry.path());
    }
    auto lmbda_byPath = [](const fs::path& first, const fs::path& second) {
        return Utils::Lower_cai(first.generic_string()) < Utils::Lower_cai(second.generic_string());
    };
    std::sort(files.begin(), files.end(), lmbda_byPath);
    return files;
}

AircraftFinder::~AircraftFinder()
{
    Stop_cai();
}

// Requests the thread to stop and waits for it.
void AircraftFinder::Stop_cai()
{
    if (mThread.Joinable_cai()) {
        mThread.RequestStop_cai();
        mThread.Join_cai();
    }
    std::lock_guard lock(mMutex);
    mRunning = false;
}

// Starts searching below `root`.
void AircraftFinder::Start_cai(const fs::path& root)
{
    Stop_cai();
    {
        std::lock_guard lock(mMutex);
        mResult.reset();
        mRunning = true;
    }
    mFoldersSeen = 0;
    auto lmbda_search = [this, root](StopToken stop) {
        std::vector<fs::path> files = FindAllAcf_cai(root, stop, &mFoldersSeen);
        std::lock_guard lock(mMutex);
        if (!stop.StopRequested_cai())
            mResult = std::move(files);
        mRunning = false;
    };
    mThread = StopThread(lmbda_search);
}

// Stops the search; its result is discarded.
void AircraftFinder::Abort_cai()
{
    Stop_cai();
    std::lock_guard lock(mMutex);
    mResult.reset();
}

// True while searching.
bool AircraftFinder::Running_cai() const
{
    std::lock_guard lock(mMutex);
    return mRunning;
}

// The finished search's result, once.
std::optional<std::vector<fs::path>> AircraftFinder::TakeResult_cai()
{
    std::lock_guard lock(mMutex);
    if (mRunning || !mResult)
        return std::nullopt;
    std::optional<std::vector<fs::path>> result = std::move(mResult);
    mResult.reset();
    return result;
}

} // namespace sc::analysis
