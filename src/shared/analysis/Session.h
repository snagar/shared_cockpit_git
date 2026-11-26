// shared_cockpit - the analysis being looked at and edited
//
// Outlives the window, so unsaved ticks survive closing it. Holds the chosen
// aircraft, its analysis (read from its file or built by a scan), whether
// there are unsaved changes, and the background scan. Drawing thread only
// (the scan itself runs in Scanner's thread).

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "shared/analysis/Analysis.h"
#include "shared/analysis/Builder.h"
#include "shared/analysis/Scanner.h"
#include "shared/store/AnalysisStore.h"

namespace sc::analysis {

class Session {
public:
    Session();

    // Chooses the aircraft (discarding unsaved changes) and reads its analysis file, if any.
    void SetAircraft_cai(const std::filesystem::path& acfFile, const std::string& displayName, bool loadedAircraft);
    // Forgets the aircraft and its analysis.
    void Clear_cai();

    // Starts reading the aircraft's files (types file from the plugin folder).
    void StartScan_cai();
    // Stops a running scan.
    void AbortScan_cai();
    // Call every frame: takes a finished scan's result. True when a new analysis arrived.
    bool Poll_cai();

    // Writes the analysis file (and its backup copy). False with LastError_cai().
    bool Save_cai();
    // Throws the unsaved changes away (reads the file again).
    void Revert_cai();

    // Ticks one control (only commands and datarefs can be ticked).
    void SetShare_cai(std::size_t group, std::size_t item, bool share);

    bool HasAircraft_cai() const                  { return !mAcfFile.empty(); }
    bool IsLoadedAircraft_cai() const             { return mLoadedAircraft; }
    const std::filesystem::path& AcfFile_cai() const { return mAcfFile; }
    const std::string& DisplayName_cai() const    { return mDisplayName; }
    const std::optional<AnalysisData>& Data_cai() const { return mData; }
    bool Dirty_cai() const                        { return mDirty; }
    bool OnDisk_cai() const                       { return mOnDisk; }
    // Increases with every change of the data (pages cache their views by it).
    std::size_t Revision_cai() const              { return mRevision; }
    const Scanner& ScannerRef_cai() const         { return mScanner; }
    bool Scanning_cai() const                     { return mScanner.Running_cai(); }

    // The aircraft files changed since the analysis was made (the .acf stamp differs).
    bool AircraftChanged_cai() const;
    // The analysis file is missing but its backup copy exists.
    bool CanRestore_cai() const;
    // Copies the backup back and reads it.
    bool Restore_cai();

    // The path of the analysis file for the chosen aircraft.
    std::filesystem::path AnalysisPath_cai() const;

    const std::string& LastError_cai() const      { return mError; }
    const std::string& LastNotice_cai() const     { return mNotice; }
    // What the last "Read again" changed (empty when there was nothing before).
    const BuildChanges& LastChanges_cai() const { return mChanges; }
    // True when `key` (ControlKey_cai) is new since the analysis before the last reading.
    bool IsNew_cai(const std::string& key) const;
    // Problems of the last scan (missing objects, unknown types, types file).
    const std::vector<std::string>& ScanProblems_cai() const { return mScanProblems; }

private:
    // Reads the analysis file of the chosen aircraft into mData.
    void ReadFile_cai();

    std::unique_ptr<store::AnalysisStore> mStore;
    Scanner                     mScanner;
    std::filesystem::path       mAcfFile;
    std::string                 mDisplayName;
    bool                        mLoadedAircraft = false;
    std::optional<AnalysisData> mData;
    bool                        mDirty   = false;
    bool                        mOnDisk  = false;
    std::size_t                 mRevision = 0;
    std::string                 mError;
    std::string                 mNotice;
    std::vector<std::string>    mScanProblems;
    ManipTypeSet                mTypes;            // as used by the running scan
    BuildChanges                mChanges;
    std::set<std::string>       mNewKeys;
};

} // namespace sc::analysis
