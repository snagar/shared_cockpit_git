// shared_cockpit - the seat row: the seat list, back to my seat, window follows me (0.9.30)
//
// One widget for the Home page's "Cockpit seats" card and the Connect page's "My seat" row, so both
// look and work the same. Picks go through SeatSync's requests (done in the next flight loop).

#pragma once

#include "shared/settings/Settings.h"

namespace sc::sync { class SeatSync; }

namespace sc::ui {

// The seat list, the back button and the window-follows toggle in the rest of the row.
// `enabled` false: shown read-only (the Home card during a session). True when `alerts` changed (save it).
bool ui_seat_row_cai(sync::SeatSync& seats, settings::AlertSettings& alerts, bool enabled = true);

// The help text of the row's (?).
const char* SeatRowHelp_cai();

} // namespace sc::ui
