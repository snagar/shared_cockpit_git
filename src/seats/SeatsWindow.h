// shared_cockpit - the Seats window: a small floating window with only the seat row (0.9.31)
//
// For someone who just wants to change seats: the seat list, back to my seat and "window follows me",
// where the seats come from, and a button for the main window. Opened from Home ("Seats"), the menu
// or shared_cockpit/toggle_seats_window. Works in 2D and VR (X-Plane puts it in VR like the main
// window) and in a session (the same as My seat on the Connect page). Independent of the Flight deck.

#pragma once

#include <functional>

#include "ImgWindow.h"

#include "shared/net/SessionHub.h"
#include "shared/settings/Settings.h"

namespace sc::sync { class SeatSync; }

namespace sc {

class SeatsWindow : public ImgWindow {
public:
    SeatsWindow(net::SessionHub& hub, settings::Settings& settings);
    ~SeatsWindow() override = default;

    SeatsWindow(const SeatsWindow&)            = delete;
    SeatsWindow& operator=(const SeatsWindow&) = delete;

    // The seats (nullptr: none, the plugin is disabled).
    void SetSeats_cai(sync::SeatSync* seats) { mSeats = seats; }
    // What "Main window" calls (from the flight loop).
    void SetToggleMain_cai(std::function<void()> toggle) { mToggleMain = std::move(toggle); }
    // From the flight loop: the button actions.
    void Tick_cai();

    inline static constexpr int kWidth  = 380;
    inline static constexpr int kHeight = 88;

protected:
    // ImgWindow: no scroll bars.
    ImGuiWindowFlags_ beforeBegin() override;
    // ImgWindow: the seat row.
    void buildInterface() override;

private:
    net::SessionHub&      mHub;
    settings::Settings&   mSettings;
    sync::SeatSync*       mSeats = nullptr;
    std::function<void()> mToggleMain;
    bool                  mMainPressed = false;
};

} // namespace sc
