// shared_cockpit - the shareability card
//
// The overall grade with its reason, then one row per system (grade chip in a
// fixed column, the system's name and, optionally, the reason). Used by the
// Home page (short) and the Analyse page (with reasons).

#pragma once

#include "shared/analysis/Analysis.h"

namespace sc::ui {

// Draws the shareability card of `data` in the current window / child.
void ui_shareability_card_cai(const analysis::AnalysisData& data, bool withReasons);

} // namespace sc::ui
