// shared_cockpit - turns a scan into an analysis, and grades it
//
// BuildAnalysis_cai groups the controls by type, keeps the user's earlier
// ticks (matched by type + refs) and applies the "kept local by default" words
// to new controls. GradeAnalysis_cai predicts how well each system can be
// shared, from the controls and Signals alone - so an analysis read from a file
// can be graded again with newer rules. No X-Plane calls.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "shared/analysis/Analysis.h"
#include "shared/analysis/ManipTypes.h"
#include "shared/analysis/ObjScan.h"

namespace sc::analysis {

// What a new reading changed compared with the analysis before it.
struct BuildChanges {
    std::vector<std::string> added;      // control keys that are new
    std::vector<std::string> removed;    // control keys that are gone
    std::size_t              unchanged = 0;
};

// A new analysis from `scan`. `previous` (may be null): the analysis saved before,
// whose ticks are kept. `aircraftName`: the name shown for the aircraft. `changes`
// (may be null) receives what changed compared with `previous`.
AnalysisData BuildAnalysis_cai(const AircraftScan& scan, const ManipTypeSet& types, const std::string& aircraftName,
                               const AnalysisData* previous, BuildChanges* changes = nullptr);

// One paragraph of the "What can be shared" explanation.
struct Explanation {
    std::string title;                   // "Flight plan / FMS - None"
    Grade       grade = Grade::Check;
    std::string text;
};

// Explains in words, per system, what the grades mean for two pilots flying together.
std::vector<Explanation> ExplainSharing_cai(const AnalysisData& data);

// Recounts the Signals that come from the controls (commands / datarefs, devices,
// avionics). notUsed and pluginsFound are kept as they are.
void CountSignals_cai(AnalysisData& data);

// Fills data.grades (predicted) from the controls and the Signals.
void GradeAnalysis_cai(AnalysisData& data);

} // namespace sc::analysis
