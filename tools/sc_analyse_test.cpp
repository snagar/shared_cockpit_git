// shared_cockpit - command-line test of the analysis (no X-Plane needed)
//
//   sc_analyse_test <sc_manip_types.ini> <out.json> <file.acf | file.obj ...>
//
// Scans the files, builds and grades the analysis, writes it as JSON, reads the
// JSON back and checks that both are the same. Prints a summary. Exit code 0 = ok.

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "fmt/format.h"
#include "fmt/ranges.h"

#include "shared/analysis/Builder.h"
#include "shared/analysis/ManipTypes.h"
#include "shared/analysis/ObjScan.h"
#include "shared/store/AnalysisStore.h"
#include "shared/util/Utils.h"

namespace fs = std::filesystem;
using namespace sc;

namespace {

// True when two analyses hold the same controls, ticks and grades.
bool SameAnalysis_cai(const analysis::AnalysisData& a, const analysis::AnalysisData& b, std::string& why)
{
    if (a.controls.size() != b.controls.size()) { why = "group count"; return false; }
    for (std::size_t g = 0; g < a.controls.size(); ++g) {
        const auto& ga = a.controls[g];
        const auto& gb = b.controls[g];
        if (ga.type != gb.type || ga.category != gb.category || ga.count != gb.count || ga.items.size() != gb.items.size()) {
            why = "group " + ga.type;
            return false;
        }
        for (std::size_t i = 0; i < ga.items.size(); ++i) {
            const auto& ca = ga.items[i];
            const auto& cb = gb.items[i];
            if (ca.share != cb.share || ca.refs != cb.refs || ca.tip != cb.tip || ca.file != cb.file) {
                why = "control " + analysis::ControlKey_cai(ga.type, ca.refs);
                return false;
            }
        }
    }
    if (a.grades.overall != b.grades.overall || a.grades.systems.size() != b.grades.systems.size()) { why = "grades"; return false; }
    for (std::size_t s = 0; s < a.grades.systems.size(); ++s) {
        if (a.grades.systems[s].grade != b.grades.systems[s].grade || a.grades.systems[s].reason != b.grades.systems[s].reason) {
            why = "grade " + a.grades.systems[s].system;
            return false;
        }
    }
    if (a.signals.notUsed != b.signals.notUsed || a.signals.avionics != b.signals.avionics ||
        a.signals.commandsSim != b.signals.commandsSim || a.signals.datarefsCustom != b.signals.datarefsCustom) {
        why = "signals";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: sc_analyse_test <types.ini> <out.json> <file.acf | file.obj ...>\n");
        return 2;
    }
    const analysis::ManipTypeSet types = analysis::ParseTypesIni_cai(Utils::ReadAll_cai(argv[1]));
    for (const std::string& problem : types.problems)
        fmt::print("types file: {}\n", problem);

    analysis::AircraftScan scan;
    const fs::path first = argv[3];
    if (Utils::Lower_cai(Utils::Utf8_cai(first.extension())) == ".acf") {
        scan = analysis::ScanAircraft_cai(first, types);
    } else {
        std::vector<fs::path> files;
        for (int i = 3; i < argc; ++i)
            files.emplace_back(argv[i]);
        scan = analysis::ScanObjFiles_cai(files, types);
        scan.acfFile = files.front();
    }
    for (const std::string& problem : scan.problems)
        fmt::print("scan: {}\n", problem);
    for (const auto& [type, lines] : scan.unsupported)
        fmt::print("unsupported: {} ({} lines)\n", type, lines);

    analysis::AnalysisData data = analysis::BuildAnalysis_cai(scan, types, Utils::Utf8_cai(first.stem()), nullptr);
    fmt::print("{}: {} controls, {} shareable, {} shared\n", data.aircraftName, data.ControlCount_cai(),
               data.ShareableCount_cai(), data.SharedCount_cai());
    for (const auto& group : data.controls)
        fmt::print("  {:<28} {:<9} {}\n", group.type, analysis::CategoryName_cai(group.category), group.count);
    for (const auto& [type, count] : data.signals.notUsed)
        fmt::print("  not used: {} {}\n", type, count);
    fmt::print("signals: commands sim {} custom {}, datarefs sim {} custom {}, devices {}, avionics [{}]\n",
               data.signals.commandsSim, data.signals.commandsCustom, data.signals.datarefsSim,
               data.signals.datarefsCustom, data.signals.deviceManips, fmt::join(data.signals.avionics, ", "));
    fmt::print("overall: {} - {}\n", analysis::OverallLabel_cai(data.grades.overall), data.grades.overallReason);
    for (const auto& system : data.grades.systems)
        fmt::print("  {:<22} {:<13} {}\n", analysis::SystemLabel_cai(system.system), analysis::GradeLabel_cai(system.grade),
                   system.reason);

    const auto store = store::MakeAnalysisStore_cai();
    const std::string text = store->ToText_cai(data);
    std::string error;
    if (text.empty() || !Utils::WriteFileSafely_cai(argv[2], text, error)) {
        fmt::print("WRITE FAILED {}\n", error);
        return 1;
    }
    analysis::AnalysisData back;
    if (!store->FromText_cai(Utils::ReadAll_cai(argv[2]), back, error)) {
        fmt::print("READ FAILED {}\n", error);
        return 1;
    }
    std::string why;
    if (!SameAnalysis_cai(data, back, why)) {
        fmt::print("ROUND TRIP DIFFERS: {}\n", why);
        return 1;
    }
    // A re-analysis keeps the earlier ticks: untick the first shareable control, rebuild.
    for (auto& group : back.controls) {
        if (!group.items.empty() && group.items.front().share) {
            group.items.front().share = !*group.items.front().share;
            break;
        }
    }
    const analysis::AnalysisData again = analysis::BuildAnalysis_cai(scan, types, data.aircraftName, &back);
    if (again.SharedCount_cai() == data.SharedCount_cai()) {
        fmt::print("MERGE FAILED: the changed tick was not kept\n");
        return 1;
    }
    fmt::print("round trip ok, merge ok, {} bytes\n", text.size());
    return 0;
}
