// shared_cockpit - reading and writing the analysis file (format independent part)

#include "shared/store/AnalysisStore.h"

#include <system_error>

#include "SharedInfo.h"
#include "shared/util/Utils.h"

namespace sc::store {

namespace fs = std::filesystem;

// <aircraft folder>/<acf name>.shared_cockpit<ext>.
fs::path AnalysisPathFor_cai(const fs::path& acfFile, const AnalysisStore& store)
{
    return acfFile.parent_path() /
           Utils::PathFromUtf8_cai(Utils::Utf8_cai(acfFile.stem()) + std::string(info::kAnalysisSuffix) + store.Extension_cai());
}

// <backup folder>/<folder>_<acf>_<hash>.shared_cockpit<ext>.
fs::path BackupPathFor_cai(const fs::path& acfFile, const fs::path& backupFolder, const AnalysisStore& store)
{
    const std::string folder = Utils::SafeFileName_cai(Utils::Utf8_cai(acfFile.parent_path().filename()));
    const std::string acf    = Utils::SafeFileName_cai(Utils::Utf8_cai(acfFile.stem()));
    const std::string hash   = Utils::Hash_cai(Utils::Utf8_cai(acfFile.lexically_normal().generic_u8string())).substr(0, 8);
    return backupFolder / (folder + "_" + acf + "_" + hash + std::string(info::kAnalysisSuffix) + store.Extension_cai());
}

// Reads the analysis of `acfFile`.
bool ReadAnalysis_cai(const fs::path& acfFile, const AnalysisStore& store, analysis::AnalysisData& data, bool& exists,
                      std::string& error)
{
    const fs::path path = AnalysisPathFor_cai(acfFile, store);
    std::error_code ec;
    exists = fs::is_regular_file(path, ec);
    if (!exists) {
        error = "no analysis file yet";
        return false;
    }
    const std::string text = Utils::ReadAll_cai(path);
    if (text.empty()) {
        error = "cannot read " + Utils::Utf8_cai(path.filename());
        return false;
    }
    analysis::AnalysisData read;
    if (!store.FromText_cai(text, read, error)) {
        error = Utils::Utf8_cai(path.filename()) + ": " + error;
        return false;
    }
    data = std::move(read);
    return true;
}

// Writes the analysis next to the aircraft, then its backup copy.
bool WriteAnalysis_cai(const fs::path& acfFile, const fs::path& backupFolder, const AnalysisStore& store,
                       const analysis::AnalysisData& data, std::string& error, std::string& warning)
{
    const std::string text = store.ToText_cai(data);
    if (text.empty()) {
        error = "could not write the analysis as text";
        return false;
    }
    if (!Utils::WriteFileSafely_cai(AnalysisPathFor_cai(acfFile, store), text, error))
        return false;
    if (!backupFolder.empty()) {
        std::error_code ec;
        fs::create_directories(backupFolder, ec);
        std::string backupError;
        if (!Utils::WriteFileSafely_cai(BackupPathFor_cai(acfFile, backupFolder, store), text, backupError))
            warning = "backup copy not written: " + backupError;
    }
    return true;
}

// True when only the backup copy exists.
bool CanRestoreBackup_cai(const fs::path& acfFile, const fs::path& backupFolder, const AnalysisStore& store)
{
    std::error_code ec;
    return !backupFolder.empty() && !fs::exists(AnalysisPathFor_cai(acfFile, store), ec) &&
           fs::is_regular_file(BackupPathFor_cai(acfFile, backupFolder, store), ec);
}

// Copies the backup back next to the aircraft.
bool RestoreBackup_cai(const fs::path& acfFile, const fs::path& backupFolder, const AnalysisStore& store,
                       std::string& error)
{
    return Utils::CopyFileSafely_cai(BackupPathFor_cai(acfFile, backupFolder, store), AnalysisPathFor_cai(acfFile, store),
                                     error);
}

} // namespace sc::store
