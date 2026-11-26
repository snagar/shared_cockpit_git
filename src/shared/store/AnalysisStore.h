// shared_cockpit - reading and writing the analysis file
//
// The rest of the plugin only sees this interface and the plain C++ types of
// shared/analysis/Analysis.h: no json (or other format) type ever leaves the store.
// To change the file format, write another AnalysisStore (e.g. IniAnalysisStore)
// and return it from MakeAnalysisStore_cai() - nothing else changes.
//
// The file lives next to the aircraft: <aircraft folder>/<acf name>.shared_cockpit.<ext>.
// Every save also writes a copy to <plugin folder>/backups/, because aircraft
// installers may replace the aircraft folder. No X-Plane calls here.

#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "shared/analysis/Analysis.h"

namespace sc::store {

class AnalysisStore {
public:
    virtual ~AnalysisStore() = default;

    // The file extension of this format, with the dot (".json").
    virtual std::string Extension_cai() const = 0;
    // Turns `text` into `data`. False with `error` when the text is not a valid analysis.
    virtual bool FromText_cai(const std::string& text, analysis::AnalysisData& data, std::string& error) const = 0;
    // Turns `data` into the file's text. Never throws.
    virtual std::string ToText_cai(const analysis::AnalysisData& data) const = 0;
};

// The store the plugin uses (JSON).
std::unique_ptr<AnalysisStore> MakeAnalysisStore_cai();

// <aircraft folder>/<acf name>.shared_cockpit<ext> for `acfFile`.
std::filesystem::path AnalysisPathFor_cai(const std::filesystem::path& acfFile, const AnalysisStore& store);

// <backup folder>/<aircraft folder name>_<acf name>_<hash>.shared_cockpit<ext>: unique per .acf path.
std::filesystem::path BackupPathFor_cai(const std::filesystem::path& acfFile, const std::filesystem::path& backupFolder,
                                        const AnalysisStore& store);

// Reads the analysis of `acfFile`. False (and `error`) when there is none or it is broken;
// `exists` tells which.
bool ReadAnalysis_cai(const std::filesystem::path& acfFile, const AnalysisStore& store, analysis::AnalysisData& data,
                      bool& exists, std::string& error);

// Writes the analysis next to the aircraft and its copy to `backupFolder` (when not
// empty). False with `error` when the aircraft's file could not be written; a failed
// copy only fills `warning`.
bool WriteAnalysis_cai(const std::filesystem::path& acfFile, const std::filesystem::path& backupFolder,
                       const AnalysisStore& store, const analysis::AnalysisData& data, std::string& error,
                       std::string& warning);

// True when the backup copy exists but the aircraft's own file does not (it can be restored).
bool CanRestoreBackup_cai(const std::filesystem::path& acfFile, const std::filesystem::path& backupFolder,
                          const AnalysisStore& store);

// Copies the backup back next to the aircraft.
bool RestoreBackup_cai(const std::filesystem::path& acfFile, const std::filesystem::path& backupFolder,
                       const AnalysisStore& store, std::string& error);

} // namespace sc::store
