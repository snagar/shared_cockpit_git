// shared_cockpit - the Home page: the steps of a shared flight
//
// Left: one big button per step (VR friendly), each with a status line.
// Right: the aircraft, its shareability summary and the session status.
// Step 2 (Connect) opens the Connect page; the flight deck comes with a later build.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

#include "shared/analysis/Session.h"
#include "shared/net/LocalAddresses.h"
#include "shared/net/SessionHub.h"
#include "shared/settings/Settings.h"

namespace sc::sync { class SeatSync; }

namespace sc::pages {

class HomePage {
public:
    HomePage(analysis::Session& session, net::SessionHub& hub, settings::Settings& settings);

    // Called by the Analyse button.
    void SetOpenAnalyse_cai(std::function<void()> open) { mOpenAnalyse = std::move(open); }
    // Called by the Flight deck button.
    void SetOpenFlightDeck_cai(std::function<void()> open) { mOpenFlightDeck = std::move(open); }
    // The seats, for the "Seats" button's status line (0.9.30).
    void SetSeats_cai(sync::SeatSync* seats) { mSeats = seats; }
    // What the "Seats" button calls: the small Seats window, the main one hides (0.9.31).
    void SetOpenSeats_cai(std::function<void()> open) { mOpenSeats = std::move(open); }
    // Called by the Connect button.
    void SetOpenConnect_cai(std::function<void()> open) { mOpenConnect = std::move(open); }
    // Points the session at the loaded aircraft when it has no unsaved changes.
    void Refresh_cai();
    // Draws the page.
    void ui_draw_cai();

private:
    // A big step button with its status line. Returns true when pressed.
    bool ui_step_button_cai(int number, const char* label, bool done, bool next, bool enabled, const std::string& status,
                            const ImVec4& statusColor);
    // The right column: aircraft, shareability, session.
    void ui_right_column_cai(float width, float height);
    // "Seats": opens the small Seats window (0.9.31; was a card, 0.9.30).
    void ui_seats_button_cai();
    // The About popup.
    void ui_popup_about_cai();

    analysis::Session&    mSession;
    net::SessionHub&      mHub;
    settings::Settings&   mSettings;
    std::function<void()> mOpenAnalyse;
    std::function<void()> mOpenConnect;
    std::function<void()> mOpenFlightDeck;
    sync::SeatSync*       mSeats = nullptr;
    std::function<void()> mOpenSeats;
    std::vector<net::LocalAddress> mAddresses;      // About: this computer's addresses and name (0.9.12)
    std::string           mMachine;
    bool                  mAskAbout = false;
};

} // namespace sc::pages
