// shared_cockpit - runtime values shared by the whole plugin

#include "Globals.h"

#include "SharedInfo.h"

namespace sc {

// The one instance of the shared values (created on first use).
Globals& Globals::get_cai()
{
    static Globals globals;
    return globals;
}

// The folder where copies of the analysis files are kept.
std::filesystem::path Globals::backupFolder_cai() const
{
    return pluginFolder / std::string(info::kBackupFolder);
}

} // namespace sc
