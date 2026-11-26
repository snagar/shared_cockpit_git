// shared_cockpit - the Session settings page (0.7)

#include "connect/SessionSettingsPage.h"

#include "fmt/format.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"

#include "shared/log/Log.h"
#include "shared/ui/Theme.h"
#include "shared/ui/Widgets.h"

namespace sc::pages {

namespace {
constexpr float kLabelW = 290.0f;   // the label column
constexpr const char* kPopupCopilot = "Copilot axes - read this first";
// The risks of copilot axes (the confirm popup and the help marker).
constexpr const char* kCopilotRisks =
    "With copilot axes on, one Crew member's stick or yoke and rudder pedals act on X-Plane's copilot side, on every "
    "X-Plane in the session. That person binds their hardware to X-Plane's Copilot Pitch, Copilot Roll and Copilot Yaw "
    "axes (Settings > Joystick). Toe brakes and the nosewheel tiller stay with the PIC.\n\n"
    "The risks:\n"
    "- Two people fly at once. Linked controls (most aircraft): the larger deflection wins - the copilot can overpower "
    "the PIC just by pushing further. Independent fly-by-wire sticks: both inputs are added - two pilots correcting the "
    "same bump double the correction.\n"
    "- Delay: the copilot's inputs arrive a little late on the other X-Planes (a few ms on a LAN, 30-150 ms over the "
    "internet), and each X-Plane flies its own aircraft, which drifts a little. Reacting to a slightly different, "
    "slightly late picture can start a pilot-induced oscillation - pitching or rolling back and forth.\n"
    "- A stick that is not centred adds a small constant input for everybody: calibrate it, give it a small dead zone.\n"
    "- A lost link centres the copilot side after 0.5 s; until then the last input stays in.\n"
    "- Only one copilot at a time: the highest-ranked Crew member who moves their hardware.\n\n"
    "Agree before the flight who flies; the PIC can use the aircraft's priority button where it has one. Leave it off "
    "unless both of you want to fly together.";
}

SessionSettingsPage::SessionSettingsPage(net::SessionHub& hub, settings::Settings& settings) : mHub(hub), mSettings(settings)
{
}

// The title row.
void SessionSettingsPage::ui_top_bar_cai(bool editable)
{
    if (ui::ui_button_cai(ICON_FA_ARROW_LEFT " Back", ImVec2(0.0f, 44.0f)) && mGoBack)
        mGoBack();
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Session settings - how the cockpit is shared");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("PIC = pilot in command: the one who has the controls");
    const net::NetSession& session = mHub.Session_cai();
    const std::string who = session.IsHost_cai() && session.Active_cai() ? "You host: changes apply now, for everybody"
                          : session.Active_cai() ? fmt::format("Set by the host, {} (read-only)", session.NameOf_cai(net::kHostId))
                                                 : std::string("Used when you host (joiners get the host's settings)");
    ImGui::TextColored(editable ? ui::theme::kTextDim : ui::theme::kChangedText, "%s", who.c_str());
    ImGui::EndGroup();
}

// A section title with its (?) explanation: "— Title (?) ———————" (the line starts after the marker).
void SessionSettingsPage::ui_section_cai(const char* title, const char* help)
{
    ImGui::Spacing();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 lineColor = ImGui::GetColorU32(ImGuiCol_Separator);
    const float lineY = ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() * 0.5f;
    const float left = ImGui::GetCursorScreenPos().x;
    const float lead = style.SeparatorTextPadding.x;          // the short line before the title, like SeparatorText
    draw->AddLine(ImVec2(left, lineY), ImVec2(left + lead - 6.0f, lineY), lineColor, style.SeparatorTextBorderSize);
    ImGui::SetCursorScreenPos(ImVec2(left + lead, ImGui::GetCursorScreenPos().y));
    ImGui::TextUnformatted(title);
    ImGui::SameLine(0.0f, 6.0f);
    ui::ui_help_marker_cai(help);
    const float from = ImGui::GetItemRectMax().x + 6.0f;
    const float to = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (to > from)
        draw->AddLine(ImVec2(from, lineY), ImVec2(to, lineY), lineColor, style.SeparatorTextBorderSize);
}

// The label column of a row.
void SessionSettingsPage::ui_label_cai(const char* label, const char* tooltip)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        // Wrapped like the (?) markers: the text has no line breaks of its own.
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(tooltip);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SameLine(kLabelW);
}

// Three choices in a row.
bool SessionSettingsPage::ui_choice3_cai(const char* id, int& value, int a, int b, int c, const char* unit)
{
    bool changed = false;
    const int choices[] = {a, b, c};
    ImGui::PushID(id);
    for (int i = 0; i < 3; ++i) {
        if (i > 0)
            ImGui::SameLine();
        const std::string text = fmt::format("{} {}", choices[i], unit);
        if (ImGui::RadioButton(text.c_str(), value == choices[i]) && value != choices[i]) {
            value = choices[i];
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

// A whole number field.
bool SessionSettingsPage::ui_number_cai(const char* id, int& value, const char* unit)
{
    ImGui::SetNextItemWidth(110.0f);
    // Applied on Enter, on leaving the field, or with the +/- buttons - not on every typed digit
    // (the rules clamp: "250" would become 50 at its first digit).
    const bool changed = ImGui::InputInt(id, &value, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::TextUnformatted(unit);
    return changed;
}

// "Joiners start as".
void SessionSettingsPage::ui_joiners_cai(bool editable)
{
    net::NetSession& session = mHub.Session_cai();
    const bool hosting = session.IsHost_cai() && session.Active_cai();
    ui_label_cai("Joiners start as", "For the people who join from now on; also on the Connect page. "
                                     "The people already in keep their seats (change them in the people list).");
    if (!editable) {
        ImGui::TextDisabled("(the host decides)");
        return;
    }
    int observer = hosting ? (session.HostSettings_cai().joinersStartAs == net::Role::Observer ? 1 : 0)
                           : (mSettings.connect.joinersObserve ? 1 : 0);
    const int before = observer;
    ImGui::RadioButton("Crew (automatic)", &observer, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Observer (I assign seats)", &observer, 1);
    if (observer == before)
        return;
    if (hosting) {
        std::string why;
        if (!session.SetJoinersStartAs_cai(observer == 1 ? net::Role::Observer : net::Role::Crew, why))
            LOG_WARN("joiners start as: {}", why);
    }
    mSettings.connect.joinersObserve = observer == 1;
    std::string error;
    if (!mSettings.Save_cai(error))
        LOG_WARN("settings not saved: {}", error);
}

// Saves and, while hosting, sends.
void SessionSettingsPage::Apply_cai(const net::SessionRules& rules)
{
    net::SessionRules clamped = rules;
    clamped.Clamp_cai();
    mSettings.rules = clamped;
    std::string error;
    if (!mSettings.Save_cai(error))
        LOG_WARN("settings not saved: {}", error);
    mHub.Session_cai().SetOwnRules_cai(clamped);
}

// Draws the page.
void SessionSettingsPage::ui_draw_cai()
{
    const net::NetSession& session = mHub.Session_cai();
    const bool editable = !session.Active_cai() || session.IsHost_cai();
    ui_top_bar_cai(editable);
    ImGui::Separator();
    // While joined: the host's rules; otherwise: mine.
    net::SessionRules rules = editable ? mSettings.rules : session.Rules_cai();
    bool changed = false;

    ImGui::BeginChild("##rules", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() - 6.0f));
    ImGui::BeginDisabled(!editable);

    ui_section_cai("Controls",
        "How switches, knobs and levers are shared.\n\n"
        "Updates while moving: a knob or lever being turned is sent this often; its final value is always "
        "sent when it stops. 10 a second looks smooth. 20 is smoother but sends twice as much while "
        "anything moves - with 4 people over the internet, 5 or 10 is the better choice.\n\n"
        "Conflict window: when two people move the same control within this time, the higher rank "
        "wins (the host, then the PIC - pilot in command, the one who has the controls - then the priority "
        "list) and the other one snaps back. Raise it "
        "when someone's ping is high (over ~300 ms), so late arrivals do not fight.");
    ui_label_cai("Knob / lever updates while moving", "How often a moving knob or lever is sent (its final value is always sent). "
                                                      "More is smoother, less is lighter on the network.");
    changed |= ui_choice3_cai("rate", rules.rate, 5, 10, 20, "/ s");
    ui_label_cai("Conflict window", "Two people change the same control within this time: the higher rank wins "
                                    "(the host, then the PIC, then the priority list). Raise it on slow links.");
    changed |= ui_choice3_cai("conflict", rules.conflictMs, 500, 1000, 2000, "ms");
    ImGui::EndDisabled();
    ui_joiners_cai(editable);
    ImGui::BeginDisabled(!editable);

    ui_section_cai("Flying together",
        "PIC = pilot in command: the one who has the controls.\n\n"
        "Follow the PIC (recommended): the PIC's aircraft leads. Its position, attitude and speeds go to "
        "everybody about 30 times a second, and every other aircraft is placed there every frame - "
        "moved ahead by the network delay and blended in smoothly, so the aircraft stay together. Their "
        "flight model keeps running (engine, instruments, sounds), and the PIC's yoke, pedals and levers "
        "still move theirs.\n\n"
        "Share the PIC's inputs only: every X-Plane flies its own flight model with the PIC's yoke, "
        "rudder, brakes and levers. Even with the same inputs two X-Planes do not fly the same way "
        "(frame rate, timing, wind, ground friction), and nothing pulls them back together - on the "
        "ground they drift apart within a minute. \"Move to PIC\" brings them back.\n\n"
        "In both, the non-PICs' own joystick and pedals are switched off while they are not the PIC.\n\n"
        "Pause: the PIC's pause is everybody's. Anybody else pausing is resumed again.");
    ui_label_cai("How the aircraft fly", "Follow the PIC keeps the aircraft together; inputs only lets each X-Plane fly on its own.");
    {
        int mode = static_cast<int>(rules.flight);
        if (ImGui::RadioButton("Follow the PIC", mode == 1) && mode != 1) {
            rules.flight = net::FlightMode::Follow;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Share the PIC's inputs only", mode == 0) && mode != 0) {
            rules.flight = net::FlightMode::Inputs;
            changed = true;
        }
    }
    {
        const bool follow = rules.flight == net::FlightMode::Follow;
        ImGui::BeginDisabled(!follow);
        ui_label_cai("Smoothing", "How fast a follower's aircraft closes a gap to the PIC's. Shorter follows more "
                                  "tightly but shows the network's jitter; longer is smoother but a little behind. "
                                  "A gap of more than 30 m on the ground (300 m in the air) is closed at once.");
        changed |= ui_choice3_cai("smoothing", rules.smoothingMs, 50, 100, 200, "ms");
        ui_label_cai("Strict", "The followers' flight model is switched off while they follow: their aircraft are "
                               "exactly where the PIC's is, but nothing of their own physics acts (gear, ground, "
                               "g-load). Try it if following looks jittery.");
        changed |= ImGui::Checkbox("Switch the followers' flight model off##strict", &rules.followStrict);
        ImGui::EndDisabled();
    }
    ui_label_cai("Fuel and payload", "The PIC's fuel per tank and payload go to everybody: on joining, after a move, and "
                                     "when they change. X-Plane 12 payload stations are used when the aircraft has them; "
                                     "otherwise the total payload and the CG.");
    changed |= ImGui::Checkbox("Share the PIC's fuel and payload##load", &rules.shareLoad);
    ui_label_cai("Handing over control", "When somebody takes control, their own yoke and rudder may be anywhere. The "
                                         "aircraft goes from the old PIC's yoke and rudder to the new PIC's hardware over "
                                         "this time instead of jumping. Throttle and brakes switch at once.");
    {
        int blend = rules.handoverBlendMs;
        if (ui_choice3_cai("handover", blend, 0, 1000, 2000, "ms")) {
            rules.handoverBlendMs = blend;
            changed = true;
        }
    }
    ui_label_cai("Copilot axes (opt-in)", "One Crew member's stick / yoke / pedals act on X-Plane's copilot side, for everybody. "
                                          "Read the risks before turning it on.");
    {
        bool copilot = rules.copilotAxes;
        if (ImGui::Checkbox("##copilot", &copilot)) {
            if (copilot && editable)
                mAskCopilot = true;                       // only after the risks were read
            else if (!copilot) {
                rules.copilotAxes = false;
                changed = true;
            }
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Send the copilot's axes too");
        ImGui::SameLine();
        ui::ui_help_marker_cai(kCopilotRisks);
        if (rules.copilotAxes) {
            ImGui::SetCursorPosX(kLabelW);
            ImGui::TextColored(ui::theme::kChangedText, ICON_FA_EXCLAMATION_TRIANGLE
                               " On: two people can fly at once. Agree who flies.");
        }
    }

    ui_section_cai("Position",
        "PIC = pilot in command: the one who has the controls.\n\n"
        "Everybody sends its position once a second (a few bytes). With Follow the PIC, the aircraft are "
        "kept together all the time; \"Move to PIC\" is needed only when an aircraft is very far away "
        "(more than 20 km - it then starts a new flight at the PIC's position).\n\n"
        "With inputs only, each X-Plane flies on its own and small differences build up. When yours is "
        "farther from the PIC's than these limits, the Connect page and the flight deck offer \"Move to "
        "PIC\" to bring it back. On the ground a few metres matter (taxiing side by side on a taxiway), "
        "in the air a few hundred do not. Lower limits offer the move more often.");
    ImGui::BeginDisabled(rules.flight == net::FlightMode::Follow);
    ui_label_cai("\"Move to PIC\" on the ground", "Offered when farther than this from the PIC, or the heading is off more, "
                                                 "or the engine state differs.");
    changed |= ui_number_cai("##ground", rules.groundM, "m   or heading");
    ImGui::SameLine();
    changed |= ui_number_cai("##heading", rules.headingDeg, "degrees");
    ui_label_cai("\"Move to PIC\" in the air", "Offered when farther than this from the PIC.");
    changed |= ui_number_cai("##air", rules.airM, "m");
    ImGui::EndDisabled();
    if (rules.flight == net::FlightMode::Follow) {
        ImGui::SetCursorPosX(kLabelW);
        ImGui::TextDisabled("(these limits are for inputs only - Follow keeps the aircraft together)");
    }
    ui_label_cai("Ask joiners to move when they join", "Right after joining, a joiner off the PIC's position is asked once "
                                                       "(with Follow: only when more than 20 km away).");
    changed |= ImGui::Checkbox("##ask", &rules.askOnJoin);

    ui_section_cai("Weather and time",
        "The host's weather and clock, written into the joiners' X-Plane.\n\n"
        "X-Plane does not change its weather at once: new values are blended in over its next weather "
        "update, so a change shows after about 1 to 2 minutes. Sending more often than every 60 s does "
        "not make it appear sooner - it only sends ~1 KB more each time, for every joiner.\n\n"
        "Sent anyway: right away to a new joiner, after a move, and when the host's weather changes "
        "(at most every 10 s). The clock is corrected only when it is off by more than the tolerance, "
        "so it does not jump every time.");
    ui_label_cai("Share the host's weather", "Joiners write the host's weather (wind, clouds, visibility, pressure, ...).");
    changed |= ImGui::Checkbox("##weather", &rules.weather);
    ImGui::BeginDisabled(!rules.weather && !rules.time);
    ui_label_cai("Send every", "Plus right away to a new joiner, after a move, and when the host's weather changes "
                               "(at most every 10 s). X-Plane rebuilds its weather about once a minute.");
    changed |= ui_choice3_cai("weathers", rules.weatherS, 10, 30, 60, "s");
    ImGui::EndDisabled();
    ui_label_cai("Share the time and date", "Joiners' clocks follow the host's.");
    changed |= ImGui::Checkbox("##time", &rules.time);
    ImGui::SameLine();
    ImGui::BeginDisabled(!rules.time);
    ImGui::TextUnformatted("  correct when off by");
    ImGui::SameLine();
    changed |= ui_number_cai("##tol", rules.timeToleranceS, "s");
    ImGui::EndDisabled();

    ImGui::EndDisabled();
    ImGui::EndChild();

    if (editable) {
        if (ui::ui_button_cai(ICON_FA_UNDO " Reset to defaults", ImVec2(0.0f, 0.0f), !(rules == net::SessionRules{}))) {
            rules = net::SessionRules{};
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Saved in shared_cockpit.ini [session]%s", session.IsHost_cai() && session.Active_cai() ? ", sent to everybody" : "");
    } else {
        ImGui::TextDisabled("%s", session.RulesFromHost_cai() ? "The host's settings, in force now." : "Waiting for the host's settings...");
    }
    if (mAskCopilot) {
        ImGui::OpenPopup(kPopupCopilot);
        mAskCopilot = false;
    }
    if (ui_popup_copilot_cai() && editable) {
        rules.copilotAxes = true;
        changed = true;
    }
    if (changed && editable)
        Apply_cai(rules);
}

// The copilot axes risks: true when "Turn on, I understand" was pressed.
bool SessionSettingsPage::ui_popup_copilot_cai()
{
    ui::ui_popup_center_next_cai();
    if (!ImGui::BeginPopupModal(kPopupCopilot, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        return false;
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
    ImGui::TextUnformatted(kCopilotRisks);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    bool on = false;
    if (ui::ui_color_button_cai(ICON_FA_CHECK " Turn on, I understand", ImVec2(220.0f, 0.0f), ui::theme::kChanged)) {
        on = true;
        LOG_INFO("copilot axes: turned on (the risks were shown)");
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ui::ui_button_cai("Cancel", ImVec2(120.0f, 0.0f)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return on;
}

} // namespace sc::pages
