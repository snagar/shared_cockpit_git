// shared_cockpit - the flight deck: a small 2D window for flying in a session
//
// Three sizes, cycled with its size button:
//   Minimal        ● 32 ms ▢                                     (about 110 x 30)
//   Line           ● Ann's session · PIC Bob        ▢ ✕          (about 310 x 54)
//                  last event, or the open question with ✓ ✗
//   Small          ● Ann's session · 3 people       ▢ ✕          (about 340 x 150)
//                  PIC / your seat, Ask for control / Take, the question, the last event
// The ● light is a button: it shows / hides the main window (on the Connect page).
// A control question grows the deck to Small until it is answered or expires.
// 2D only: in VR the main window is used. Closing the deck does not end the session.
// Self-decorated (no title bar); the whole window drags.

#pragma once

#include <chrono>

#include <functional>

#include "ImgWindow.h"

#include "shared/net/SessionHub.h"
#include "shared/settings/Settings.h"

namespace sc::sync { class SeatSync; }

namespace sc {

// The deck's content, without the window (so it can be drawn and tested on its own).
class FlightDeckPanel {
public:
    enum class Size { Minimal = 0, Line = 1, Small = 2 };

    explicit FlightDeckPanel(net::SessionHub& hub) : mHub(hub) {}

    // Draws the deck in `size`.
    void ui_draw_cai(Size size);
    // True once after the light / the size button / the close button was pressed.
    bool TakeLightPressed_cai() { return Take_cai(mLightPressed); }
    bool TakeSizePressed_cai() { return Take_cai(mSizePressed); }
    bool TakeClosePressed_cai() { return Take_cai(mClosePressed); }
    // The seat row's actions (0.9.6): a seat picked in the list (-1 none, kNoSeat: no seat) and "Back to my seat".
    int  TakeSeatPick_cai() { const int pick = mSeatPick; mSeatPick = -1; return pick; }
    bool TakeSeatBack_cai() { return Take_cai(mSeatBack); }
    // The seats (null: no seat row).
    void SetSeats_cai(const sync::SeatSync* seats) { mSeats = seats; }

    // The deck's compact style (small padding: no empty band at the top). The deck has its own
    // ImGui context, so this does not change the main window.
    static void ApplyStyle_cai();
    // Width / height of a size (boxels).
    static int WidthOf_cai(Size size);
    static int HeightOf_cai(Size size);

private:
    // Returns `flag` and clears it.
    static bool Take_cai(bool& flag)
    {
        const bool was = flag;
        flag = false;
        return was;
    }
    // The status light (a button). True when pressed.
    bool ui_light_cai(float radius);
    // The size button, then (not Minimal) the close button.
    void ui_corner_buttons_cai(Size size);
    // The open question with Yes / No (false = no question).
    bool ui_question_cai();
    // The move question after joining (when no control question is open).
    bool ui_move_question_cai();
    // True while "Move to the PIC" applies.
    bool ShowMove_cai() const;
    // The last event, one line.
    void ui_last_event_cai();
    // Ask for control / Take control.
    void ui_control_buttons_cai();
    // My seat: a list of the seats (and who sits where), Back to my seat.
    void ui_seat_row_cai();

    net::SessionHub& mHub;
    bool mLightPressed = false;
    bool mSizePressed = false;
    bool mClosePressed = false;
    int  mSeatPick = -1;
    std::uint8_t mPicSeen = 0xFF;                    // the PIC last frame ("you have control")
    std::chrono::steady_clock::time_point mPicSince{};
    bool mSeatBack = false;
    const sync::SeatSync* mSeats = nullptr;
};

class FlightDeckWindow : public ImgWindow {
public:
    using Size = FlightDeckPanel::Size;

    FlightDeckWindow(net::SessionHub& hub, settings::Settings& settings);
    ~FlightDeckWindow() override = default;

    FlightDeckWindow(const FlightDeckWindow&)            = delete;
    FlightDeckWindow& operator=(const FlightDeckWindow&) = delete;

    // Called by the light (shows / hides the main window). Runs from the flight loop.
    void SetToggleMain_cai(std::function<void()> toggle) { mToggleMain = std::move(toggle); }
    // The seats (the seat row; its buttons act from the flight loop).
    void SetSeats_cai(sync::SeatSync* seats) { mSeats = seats; mPanel.SetSeats_cai(seats); }
    // From the flight loop: the button actions, growing for questions, applying a new size.
    void Tick_cai();
    // Remembers where the deck is and its size, and saves the settings.
    void Remember_cai();

protected:
    // ImgWindow: no scroll bars, no title.
    ImGuiWindowFlags_ beforeBegin() override;
    // ImgWindow: draws the panel in the size the window has.
    void buildInterface() override;

private:
    // The size to show now (Small while a question is open).
    Size Shown_cai() const;
    // Resizes the window to `size`, keeping its top-left corner.
    void Apply_cai(Size size);
    // The X-Plane window changed size: the deck goes to the nearer side edge, same height.
    void FollowScreen_cai();

    net::SessionHub&      mHub;
    settings::Settings&   mSettings;
    FlightDeckPanel       mPanel;
    std::function<void()> mToggleMain;
    sync::SeatSync*       mSeats = nullptr;
    Size                  mChosen = Size::Small;      // what the user picked
    Size                  mApplied = Size::Small;     // what the window is now
    int                   mScreen[4] = {0, 0, 0, 0};  // the screen bounds last seen (left, top, right, bottom)
    int                   mLastDeck[4] = {0, 0, 0, 0}; // the deck's geometry while those bounds were current
};

} // namespace sc
