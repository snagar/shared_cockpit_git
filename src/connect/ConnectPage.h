// shared_cockpit - the Connect page: host or join a session, the people, the checks
//
//   top      Home, "Step 2 - Connect", the aircraft, the analysis state
//            a question bar when someone asks for / offers control (Yes / No)
//   left     your name, then step by step: "Pick an action" (Host a session | Join a flight),
//            then only the picked form: "Connect to the host" (address, port, password, recent,
//            Connect to host) or "Host setup" (port, password, max people, joiners start as).
//            While connecting: progress + Cancel; in a session: a summary + notices.
//   right    the people (seat, control, ping, actions always visible),
//            Ask for / Take control, "Before you fly" checks
//   bottom   the status, Leave / End session, Flight deck, the last events
// The session lives in SessionHub: closing this window does not end it.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

#include "shared/analysis/Session.h"
#include "shared/net/LocalAddresses.h"
#include "shared/net/SessionHub.h"
#include "shared/settings/Settings.h"

namespace sc::pages {

class ConnectPage {
public:
    ConnectPage(analysis::Session& analysis, net::SessionHub& hub, settings::Settings& settings);

    // Called by the Home button.
    // What the gear button calls (the Session settings page).
    void SetOpenSettings_cai(std::function<void()> open) { mOpenSettings = std::move(open); }
    void SetGoHome_cai(std::function<void()> goHome) { mGoHome = std::move(goHome); }
    // Called by the Flight deck button (empty = the button is disabled).
    void SetOpenFlightDeck_cai(std::function<void()> open) { mOpenFlightDeck = std::move(open); }
    // Called when the page is shown: reads this computer's addresses again.
    void Refresh_cai();
    // Draws the page.
    void ui_draw_cai();

private:
    enum class Mark { Ok, Warn, Bad, Info };
    enum class Action { None, Host, Join };    // step 1 of the page: what the user picked

    // Top bar: Home, title, aircraft, analysis state.
    void ui_top_bar_cai();
    // One bar per open question, with Yes / No.
    void ui_questions_cai();
    // The left column, by session state.
    void ui_left_cai(float width, float height);
    // Name input (or the name, when in a session).
    void ui_name_cai();
    // Step 1: "Pick an action" - Host a session | Join a flight.
    void ui_mode_cai();
    // The join form and its button.
    void ui_join_form_cai();
    // The host form and its button.
    void ui_host_form_cai();
    // Connecting / handshaking: progress and Cancel.
    void ui_connecting_cai();
    // In a session: summary, notice box, addresses.
    void ui_in_session_cai();
    // This computer's addresses (for the host to tell the others).
    void ui_addresses_cai();
    // The right column: people, control, checks.
    void ui_right_cai(float width, float height);
    // The people table.
    void ui_people_cai();
    // The priority of one row: its number, and up / down for the master.
    void ui_priority_cai(const net::Person& p, const std::vector<net::Person>& people);
    // The question after joining: move to the PIC's position?
    void ui_move_question_cai();
    // "412 m from Ann (PIC), heading 87° off, ...".
    static std::string OffText_cai(const net::PositionStats& pos);
    // Position and weather lines of "Before you fly".
    void ui_position_cai();
    // A tooltip list: title, then at most 20 names.
    static std::string ListText_cai(const char* title, const std::vector<std::string>& names);
    // The action buttons of one person's row.
    void ui_person_actions_cai(const net::Person& person);
    // Ask for control / Take control.
    void ui_control_buttons_cai();
    // "Before you start" / "Before you fly".
    void ui_checks_cai();
    // The "Sharing" box (counts of the control sharing).
    void ui_sharing_cai();
    // A check line with a button at the right end (the tooltip on the text). True when pressed.
    bool ui_check_button_line_cai(Mark mark, const std::string& text, const char* button, const std::string& tooltip = {});
    // One check line with its mark.
    void ui_check_line_cai(Mark mark, const std::string& text);
    // Status, Leave, Flight deck and the feed.
    void ui_bottom_cai();
    // "Remove <name>?" confirmation.
    void ui_popup_remove_cai();

    // Starts hosting with the form's values.
    void DoHost_cai();
    // Joins with the form's values.
    void DoJoin_cai();
    // Who this simulator is (name, loaded aircraft, saved analysis).
    net::LocalInfo MakeLocal_cai() const;
    // True when this simulator can host (else `why`).
    bool CanHost_cai(std::string& why) const;
    // True when this simulator can join (else `why`).
    bool CanJoin_cai(std::string& why) const;
    // True when the analysis on the Analyse page is the loaded aircraft's, saved.
    bool AnalysisReady_cai() const;
    // Writes the settings file (errors only logged).
    void SaveSettings_cai();
    // Reports an action that did not work in the feed.
    void Report_cai(bool ok, const std::string& why);

    analysis::Session&   mAnalysis;
    net::SessionHub&     mHub;
    settings::Settings&  mSettings;
    std::function<void()> mGoHome;
    std::function<void()> mOpenSettings;
    std::function<void()> mOpenFlightDeck;

    Action       mAction = Action::None;     // not saved: every plugin start begins at step 1
    std::string  mJoinAddress;
    std::string  mJoinPassword;
    std::string  mHostPassword;
    bool         mShowPassword = false;
    std::string  mNotice;
    std::vector<net::LocalAddress> mAddresses;
    std::chrono::steady_clock::time_point mStartedAt;
    std::size_t  mSeenFeed = 0;
    int          mScrollFeed = 0;           // frames left to scroll the feed to its end
    std::uint8_t mRemoveId = net::kNoId;
    bool         mAskRemove = false;
};

} // namespace sc::pages
