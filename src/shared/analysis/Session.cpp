// shared_cockpit - the analysis being looked at and edited

#include "shared/analysis/Session.h"

#include "fmt/format.h"

#include "Globals.h"
#include "shared/log/Log.h"
#include "shared/analysis/Builder.h"
#include "shared/util/Utils.h"

namespace sc::analysis {

namespace fs = std::filesystem;

Session::Session()
    : mStore(store::MakeAnalysisStore_cai())
{
}

// Chooses the aircraft and reads its analysis file.
void Session::SetAircraft_cai(const fs::path& acfFile, const std::string& displayName, bool loadedAircraft)
{
    mScanner.Abort_cai();
    mAcfFile        = acfFile;
    mDisplayName    = displayName;
    mLoadedAircraft = loadedAircraft;
    mScanProblems.clear();
    ReadFile_cai();
    LOG_INFO("aircraft {} ({}), analysis {}", Utils::Utf8_cai(acfFile), loadedAircraft ? "loaded" : "other",
             mOnDisk ? "read" : "not found");
}

// Forgets the aircraft and its analysis.
void Session::Clear_cai()
{
    mScanner.Abort_cai();
    mAcfFile.clear();
    mDisplayName.clear();
    mLoadedAircraft = false;
    mData.reset();
    mDirty  = false;
    mOnDisk = false;
    mError.clear();
    mNotice.clear();
    mScanProblems.clear();
    ++mRevision;
}

// Reads the analysis file of the chosen aircraft into mData.
void Session::ReadFile_cai()
{
    mData.reset();
    mDirty  = false;
    mOnDisk = false;
    mError.clear();
    mNotice.clear();
    mChanges = {};
    mNewKeys.clear();
    ++mRevision;
    if (mAcfFile.empty())
        return;
    AnalysisData data;
    bool exists = false;
    std::string error;
    if (store::ReadAnalysis_cai(mAcfFile, *mStore, data, exists, error)) {
        mData   = std::move(data);
        mOnDisk = true;
    } else if (exists) {
        mError = error;
        LOG_WARN("{}", error);
    }
}

// Starts reading the aircraft's files.
void Session::StartScan_cai()
{
    if (mAcfFile.empty())
        return;
    fs::path usedFile;
    mTypes = LoadTypesFile_cai(Globals::get_cai().pluginFolder, usedFile);
    mScanProblems = mTypes.problems;
    for (const std::string& problem : mTypes.problems)
        LOG_WARN("{}: {}", Utils::Utf8_cai(usedFile.filename()), problem);
    mError.clear();
    mNotice.clear();
    mScanner.Start_cai(mAcfFile, mTypes);
    LOG_INFO("scan started: {} ({} types)", Utils::Utf8_cai(mAcfFile), mTypes.types.size());
}

// Stops a running scan.
void Session::AbortScan_cai()
{
    mScanner.Abort_cai();
    mNotice = "Reading stopped";
    LOG_INFO("scan aborted");
}

// Takes a finished scan's result and builds the new analysis from it.
bool Session::Poll_cai()
{
    std::optional<AircraftScan> scan = mScanner.TakeResult_cai();
    if (!scan)
        return false;
    if (!scan->error.empty()) {
        mError = scan->error;
        LOG_ERROR("{}", scan->error);
        return false;
    }
    for (const std::string& problem : scan->problems)
        mScanProblems.push_back(problem);
    for (const auto& [type, lines] : scan->unsupported)
        mScanProblems.push_back(fmt::format("unknown manipulator type {} ({} lines) - not listed", type, lines));
    mChanges = {};
    AnalysisData built = BuildAnalysis_cai(*scan, mTypes, mDisplayName, mData ? &*mData : nullptr, &mChanges);
    mNewKeys = std::set<std::string>(mChanges.added.begin(), mChanges.added.end());
    if (mData) {
        mNotice = fmt::format("{} new, {} removed, {} unchanged", mChanges.added.size(), mChanges.removed.size(),
                              mChanges.unchanged);
        for (const std::string& key : mChanges.removed)
            mScanProblems.push_back("removed: " + key);
        LOG_INFO("read again: {}", mNotice);
    }
    LOG_INFO("scan done: {} files, {} controls, {} shared, overall {}", built.files.size(), built.ControlCount_cai(),
             built.SharedCount_cai(), OverallName_cai(built.grades.overall));
    mData  = std::move(built);
    mDirty = true;
    ++mRevision;
    return true;
}

// Writes the analysis file and its backup copy.
bool Session::Save_cai()
{
    if (!mData || mAcfFile.empty())
        return false;
    std::string error, warning;
    if (!store::WriteAnalysis_cai(mAcfFile, Globals::get_cai().backupFolder_cai(), *mStore, *mData, error, warning)) {
        mError = "Not saved: " + error;
        LOG_ERROR("{}", mError);
        return false;
    }
    if (!warning.empty())
        LOG_WARN("{}", warning);
    mDirty  = false;
    mOnDisk = true;
    mError.clear();
    mNotice = fmt::format("Saved {}", Utils::Utf8_cai(AnalysisPath_cai().filename()));
    LOG_INFO("saved {}", Utils::Utf8_cai(AnalysisPath_cai()));
    return true;
}

// True when `key` is new since the analysis before the last reading.
bool Session::IsNew_cai(const std::string& key) const
{
    return mNewKeys.contains(key);
}

// Throws the unsaved changes away.
void Session::Revert_cai()
{
    ReadFile_cai();
    mNotice = "Changes discarded";
}

// Ticks one control.
void Session::SetShare_cai(std::size_t group, std::size_t item, bool share)
{
    if (!mData || group >= mData->controls.size() || item >= mData->controls[group].items.size())
        return;
    Control& control = mData->controls[group].items[item];
    if (!control.share || *control.share == share)
        return;
    control.share = share;
    mDirty = true;
    ++mRevision;
}

// The aircraft's .acf changed since the analysis was made.
bool Session::AircraftChanged_cai() const
{
    return mData && mOnDisk && !mData->acfStamp.empty() && mData->acfStamp != Utils::FileStamp_cai(mAcfFile);
}

// The analysis file is missing but its backup copy exists.
bool Session::CanRestore_cai() const
{
    return !mAcfFile.empty() && store::CanRestoreBackup_cai(mAcfFile, Globals::get_cai().backupFolder_cai(), *mStore);
}

// Copies the backup back and reads it.
bool Session::Restore_cai()
{
    std::string error;
    if (!store::RestoreBackup_cai(mAcfFile, Globals::get_cai().backupFolder_cai(), *mStore, error)) {
        mError = "Not restored: " + error;
        return false;
    }
    ReadFile_cai();
    mNotice = "Restored from the backup copy";
    LOG_INFO("restored {} from its backup", Utils::Utf8_cai(AnalysisPath_cai()));
    return true;
}

// The path of the analysis file for the chosen aircraft.
fs::path Session::AnalysisPath_cai() const
{
    return mAcfFile.empty() ? fs::path() : store::AnalysisPathFor_cai(mAcfFile, *mStore);
}

} // namespace sc::analysis
