// shared_cockpit - the Session settings page (0.7)
//
// The session's rules (net::SessionRules): how the controls, the position and the weather are
// shared. The host edits them (and anybody while not in a session: the rules used when hosting);
// changes are saved to shared_cockpit.ini [session] and, while hosting, sent to everybody at once.
// A joiner sees the host's rules, read-only. Opened from the Connect page's gear button.

#pragma once

#include <functional>

#include "shared/net/SessionHub.h"
#include "shared/settings/Settings.h"

namespace sc::pages {

class SessionSettingsPage {
public:
    SessionSettingsPage(net::SessionHub& hub, settings::Settings& settings);

    // Draws the page.
    void ui_draw_cai();
    // What "Back" calls (the Connect page).
    void SetGoBack_cai(std::function<void()> back) { mGoBack = std::move(back); }

private:
    // The title row: Back, the title, who decides.
    void ui_top_bar_cai(bool editable);
    // A section title with its (?) explanation.
    static void ui_section_cai(const char* title, const char* help);
    // The label column of a row (and its tooltip).
    static void ui_label_cai(const char* label, const char* tooltip);
    // Three choices in a row; true when changed.
    static bool ui_choice3_cai(const char* id, int& value, int a, int b, int c, const char* unit);
    // A whole number field; true when changed (clamped by the rules afterwards).
    static bool ui_number_cai(const char* id, int& value, const char* unit);
    // "Joiners start as" (hosting: live, like the Connect page; else: for the next hosting).
    void ui_joiners_cai(bool editable);
    // The copilot axes risks: true when "Turn on, I understand" was pressed.
    bool ui_popup_copilot_cai();
    // Alerts (pulse, chimes): this computer's own, always editable (0.9.12).
    void ui_alerts_cai();
    // Saves and, while hosting, sends.
    void Apply_cai(const net::SessionRules& rules);

    net::SessionHub&      mHub;
    settings::Settings&   mSettings;
    std::function<void()> mGoBack;
    bool                  mAskCopilot = false;    // open the copilot axes risks
};

} // namespace sc::pages

