// shared_cockpit - the Connect page: host or join a session, the people, the checks

#include "connect/ConnectPage.h"

#include <algorithm>
#include <cfloat>
#include <bit>

#include "fmt/format.h"
#include "IconsFontAwesome5.h"
#include "imgui_stdlib.h"

#include "Globals.h"
#include "SharedInfo.h"
#include "shared/analysis/Analysis.h"
#include "shared/log/Log.h"
#include "shared/sync/SeatSync.h"
#include "shared/ui/SeatRow.h"
#include "shared/ui/Widgets.h"
#include "shared/util/Utils.h"
#include "shared/xp/XPlane.h"

namespace sc::pages {

namespace {

constexpr float       kLeftWidth    = 350.0f;
constexpr float       kButtonHeight = 40.0f;       // large targets for VR controllers
constexpr float       kModeHeight   = 34.0f;       // the Host | Join switch
constexpr float       kRowButtonH   = 24.0f;
constexpr float       kLabelWidth   = 124.0f;
constexpr float       kFeedLines    = 2.0f;
constexpr float       kFeedPadY     = 4.0f;
constexpr const char* kPopupRemove  = "Remove from the session?";

// The height of the feed box: kFeedLines lines and a small padding.
float FeedHeight_cai()
{
    return ImGui::GetTextLineHeightWithSpacing() * kFeedLines + kFeedPadY * 2.0f + 2.0f;
}

// The colour of a session state (status dot / strip).
ImVec4 StateColor_cai(net::SessionState state)
{
    switch (state) {
        case net::SessionState::Hosting:
        case net::SessionState::InSession:   return ui::theme::kSaved;
        case net::SessionState::Connecting:
        case net::SessionState::Handshaking: return ui::theme::kChanged;
        case net::SessionState::Failed:
        case net::SessionState::Lost:        return ui::theme::kError;
        case net::SessionState::Idle:        break;
    }
    return ui::theme::kButtonOff;
}

// A label in front of an input, aligned with the others; `help`: a (?) after it (0.9.28).
void ui_label_cai(const char* text, const char* help = nullptr)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    if (help) {
        ImGui::SameLine(0.0f, 4.0f);
        ui::ui_help_marker_cai(help);
    }
    ImGui::SameLine(kLabelWidth);
}

// A port input (1..65535) bound to `port`. True when changed.
bool ui_port_input_cai(const char* id, std::uint16_t& port)
{
    int value = port;
    ImGui::SetNextItemWidth(110.0f);
    if (!ImGui::InputInt(id, &value, 0, 0))
        return false;
    port = static_cast<std::uint16_t>(std::clamp(value, 1, 65535));
    return true;
}

// The text colour of a feed line.
const ImVec4& FeedColor_cai(net::SessionEvent::Kind kind)
{
    switch (kind) {
        case net::SessionEvent::Kind::Warning:
        case net::SessionEvent::Kind::Declined:       return ui::theme::kChangedText;
        case net::SessionEvent::Kind::ControlChanged: return ui::theme::kButtonActive;
        case net::SessionEvent::Kind::State:          return ui::theme::kTextDim;
        default:                                      return ui::theme::kText;
    }
}

// "other .acf version, uses the host's analysis" for a person's flags (empty = none).
std::string FlagsText_cai(std::uint8_t flags)
{
    std::string text;
    auto lmbda_add = [&text](const char* part) { text += (text.empty() ? "" : ", ") + std::string(part); };
    if (flags & net::kFlagAcfVersion)
        lmbda_add("other version of the aircraft (.acf differs)");
    if (flags & net::kFlagMissingControls)
        lmbda_add("some shared controls are missing in that aircraft");
    return text;
}

} // namespace

ConnectPage::ConnectPage(analysis::Session& analysis, net::SessionHub& hub, settings::Settings& settings)
    : mAnalysis(analysis), mHub(hub), mSettings(settings)
{
    if (!mSettings.connect.recent.empty())
        mJoinAddress = mSettings.connect.recent.front();
    if (mSettings.connect.name.empty()) {
        if (const char* user = std::getenv("USER"))
            mSettings.connect.name = user;
        else if (const char* winUser = std::getenv("USERNAME"))
            mSettings.connect.name = winUser;
    }
}

// Reads the addresses again.
void ConnectPage::Refresh_cai()
{
    mAddresses = net::LocalAddresses_cai();
    mMachine = net::MachineName_cai();
}

// ======================================================================================
//  Draw
// ======================================================================================

// Draws the page.
void ConnectPage::ui_draw_cai()
{
    WatchEnd_cai();
    ui_top_bar_cai();
    ui_network_info_cai();                          // under the top bar in every state: it never moves (0.9.19)
    ui_questions_cai();
    ImGui::Separator();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float bottomH = kButtonHeight + FeedHeight_cai() + style.ItemSpacing.y * 2.0f;
    const float areaH = std::max(120.0f, ImGui::GetContentRegionAvail().y - bottomH - style.ItemSpacing.y);
    ui_left_cai(kLeftWidth, areaH);
    ImGui::SameLine();
    ui_right_cai(ImGui::GetContentRegionAvail().x, areaH);
    ui_bottom_cai();
    if (mAskRemove) {
        ImGui::OpenPopup(kPopupRemove);
        mAskRemove = false;
    }
    ui_popup_remove_cai();
}

// Top bar.
void ConnectPage::ui_top_bar_cai()
{
    if (ui::ui_button_cai(ICON_FA_HOME " Home", ImVec2(0.0f, 44.0f)) && mGoHome)
        mGoHome();
    ImGui::SameLine();
    if (ui::ui_button_cai(ICON_FA_COG "##settings", ImVec2(44.0f, 44.0f)) && mOpenSettings)
        mOpenSettings();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Session settings: how the controls, the position and the weather are shared\n"
                          "(the host decides; joiners see them read-only)");
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Step 2 - Connect: host or join");
    const auto loaded = xp::GetUserAircraft_cai();
    ui::ui_text_ellipsis_cai(loaded ? fmt::format(ICON_FA_PLANE "  {}  ({})", loaded->displayName,
                                                  Utils::Utf8_cai(loaded->acfFile.filename()))
                                    : std::string("No aircraft loaded"),
                             ImGui::GetContentRegionAvail().x - 230.0f, &ui::theme::kTextDim);
    ImGui::EndGroup();
    const char* chip = AnalysisReady_cai() ? ICON_FA_CHECK " Analysis saved"
                       : mAnalysis.Dirty_cai() ? ICON_FA_EXCLAMATION_TRIANGLE " Analysis not saved"
                                               : ICON_FA_EXCLAMATION_TRIANGLE " No analysis";
    const ImVec4& chipColor = AnalysisReady_cai() ? ui::theme::kSaved : ui::theme::kChanged;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ui::ChipWidth_cai(chip) - 30.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
    ui::ui_chip_cai(chip, chipColor);
    ImGui::SameLine();
    ui::ui_help_marker_cai("Host: you open the session, the others join you. Your saved analysis decides what is "
                           "shared, for everybody. Join: type the host's address - the LAN address at home, the "
                           "ZeroTier or Tailscale address over the internet, or the host's public address when the "
                           "host forwarded the UDP port. Closing this window does not end the session.");
}

// One bar per open question.
void ConnectPage::ui_questions_cai()
{
    net::NetSession& session = mHub.Session_cai();
    const auto now = std::chrono::steady_clock::now();
    std::vector<net::Question> questions = session.Questions_cai();      // a copy: answering changes the list
    for (const net::Question& q : questions) {
        const long long left = std::max<long long>(0,
            std::chrono::duration_cast<std::chrono::seconds>(q.until - now).count());
        const std::string text = fmt::format(ICON_FA_HAND_PAPER "  {} {}   ({} s)", q.fromName,
                                             q.kind == net::Question::Kind::Request ? "asks you for control"
                                                                                    : "offers you control",
                                             left);
        ImGui::PushID(q.fromId);
        const float buttonsW = 2.0f * 90.0f + ImGui::GetStyle().ItemSpacing.x;
        ui::ui_status_strip_cai(text, ui::ui_pulse_color_cai(ui::theme::kChanged), ui::theme::kTextOnColor,
                                ImGui::GetContentRegionAvail().x - buttonsW - ImGui::GetStyle().ItemSpacing.x, 34.0f);
        ImGui::SameLine();
        std::string why;
        if (ui::ui_color_button_cai(ICON_FA_CHECK " Yes", ImVec2(90.0f, 34.0f), ui::ui_pulse_color_cai(ui::theme::kSaved)))
            Report_cai(session.AnswerControl_cai(q.fromId, true, why), why);
        ImGui::SameLine();
        if (ui::ui_color_button_cai(ICON_FA_TIMES " No", ImVec2(90.0f, 34.0f), ui::theme::kError))
            Report_cai(session.AnswerControl_cai(q.fromId, false, why), why);
        ImGui::PopID();
    }
    ui_move_question_cai();
}

// The question after joining: move to the PIC's position?
void ConnectPage::ui_move_question_cai()
{
    net::PositionStats& pos = mHub.Position_cai();
    if (!pos.ask || !mHub.Session_cai().Active_cai())
        return;
    const float buttonsW = 2.0f * 100.0f + ImGui::GetStyle().ItemSpacing.x;
    ui::ui_status_strip_cai(fmt::format(ICON_FA_MAP_MARKER_ALT "  {} - move there?", OffText_cai(pos)),
                            ui::theme::kChanged, ui::theme::kTextOnColor,
                            ImGui::GetContentRegionAvail().x - buttonsW - ImGui::GetStyle().ItemSpacing.x, 34.0f);
    ImGui::SameLine();
    if (ui::ui_color_button_cai(ICON_FA_CHECK " Move##move", ImVec2(100.0f, 34.0f), ui::theme::kSaved))
        pos.moveRequested = true;
    ImGui::SameLine();
    if (ui::ui_color_button_cai(ICON_FA_TIMES " Not now##move", ImVec2(100.0f, 34.0f), ui::theme::kError)) {
        pos.ask = false;
        LOG_INFO("position: not moving now (the Connect page keeps a Move button while off)");
    }
}

// "412 m from Ann (PIC), heading 87° off, engines off vs running".
std::string ConnectPage::OffText_cai(const net::PositionStats& pos)
{
    std::string text = fmt::format("{} from {} (PIC)", Utils::DistanceText_cai(pos.distanceM), pos.picName);
    if (pos.picOnGround && pos.headingOff >= 1.0f)
        text += fmt::format(", heading {:.0f}° off", pos.headingOff);
    if (pos.enginesDiffer)
        text += ", engines differ";
    return text;
}

// The left column.
void ConnectPage::ui_left_cai(float width, float height)
{
    ImGui::BeginChild("##left", ImVec2(width, height));
    const net::NetSession& session = mHub.Session_cai();
    switch (session.State_cai()) {
        case net::SessionState::Connecting:
        case net::SessionState::Handshaking:
            ui_connecting_cai();
            break;
        case net::SessionState::Hosting:
        case net::SessionState::InSession:
            ui_in_session_cai();
            break;
        case net::SessionState::Idle:
        case net::SessionState::Failed:
        case net::SessionState::Lost:
            ui_name_cai();
            ui_mode_cai();
            // Failed / Lost: the red status bar at the bottom says why (0.9.18: no second strip here).
            if (mAction == Action::Host)
                ui_host_form_cai();
            else if (mAction == Action::Join)
                ui_join_form_cai();
            else
                ui::ui_wrapped_text_cai("Host: you open the session and the others join you.\n"
                                        "Join a flight: connect to someone who is hosting.",
                                        &ui::theme::kTextDim);
            break;
    }
    ImGui::EndChild();
}

// The top row: "Your network info:", the purple box (a click copies a value), Refresh, help.
void ConnectPage::ui_network_info_cai()
{
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);  // room for the box's top line
    ImGui::TextDisabled("Your network info:");
    const std::string help = fmt::format(
        "Tell the others your name or address. Click either to copy it.\n\n"
        "On a home network (LAN) they join with the LAN address, or often with the name. With ZeroTier or "
        "Tailscale, use that address. Over the internet without them: forward UDP port {} on your router and "
        "give your public address.",
        mHub.Session_cai().HostSettings_cai().port);
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4.0f);  // the box's text on the label's line
    const float rowY = ImGui::GetCursorPosY();
    if (ui::ui_my_addresses_cai(mAddresses, mMachine, help.c_str(), true))
        Refresh_cai();
    // Donate, small, under the analysis chip (0.9.23); left out when the row is too narrow.
    const float donateW = 96.0f;
    const float donateX = ImGui::GetContentRegionMax().x - 30.0f - donateW;
    const float afterRow = ImGui::GetCursorPosY();
    ImGui::SameLine();
    if (ImGui::GetCursorPosX() + 12.0f < donateX) {
        ImGui::SetCursorPos(ImVec2(donateX, rowY));
        ui::ui_donate_button_cai("connect", ImVec2(donateW, 0.0f), true);
    }
    ImGui::SetCursorPosY(std::max(afterRow, ImGui::GetCursorPosY()));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// Every frame: notes the moment the session ends or fails (0.9.21: also while no form is shown,
// so the red header comes back after each kick).
void ConnectPage::WatchEnd_cai()
{
    const net::SessionState state = mHub.Session_cai().State_cai();
    const bool ended = state == net::SessionState::Lost || state == net::SessionState::Failed;
    if (ended && !mEndedSeen)
        mEndedAt = std::chrono::steady_clock::now();
    mEndedSeen = ended;
}

// A form's header; red with a warning triangle for 5 s after a session ended or failed (0.9.20),
// or until Host / Connect again is pressed.
void ConnectPage::ui_form_header_cai(const char* title)
{
    const bool alert = mEndedSeen && std::chrono::steady_clock::now() - mEndedAt < std::chrono::seconds(5);
    if (!alert) {
        ImGui::SeparatorText(title);
        return;
    }
    ImGui::TextColored(ui::theme::kWarnYellow, ICON_FA_EXCLAMATION_TRIANGLE);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ui::theme::kErrorText);
    ImGui::PushStyleColor(ImGuiCol_Separator, ui::theme::kError);
    ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextBorderSize, 2.0f);
    ImGui::SeparatorText(title);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

// Name input.
void ConnectPage::ui_name_cai()
{
    ui_label_cai("Your name");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##name", "shown to the others", &mSettings.connect.name))
        if (mSettings.connect.name.size() > net::kMaxNameLength)
            mSettings.connect.name.resize(net::kMaxNameLength);
    if (ImGui::IsItemDeactivatedAfterEdit())
        SaveSettings_cai();
}

// Step 1: pick an action. Nothing is picked when the page opens; the picked one is
// blue with a check, the other stays a light, clickable choice (it never looks disabled).
void ConnectPage::ui_mode_cai()
{
    ImGui::SeparatorText("Pick an action");
    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    auto lmbda_choice = [w](const char* label, const char* picked, bool selected) {
        const ImVec4& bg = selected ? ui::theme::kButton : ui::theme::kInputBg;
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? ui::theme::kButtonHover : ui::theme::kPopupBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, selected ? ui::theme::kButtonActive : ui::theme::kPanelBgDark);
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? ui::theme::kTextOnColor : ui::theme::kText);
        ImGui::PushStyleColor(ImGuiCol_Border, ui::theme::kButton);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
        const bool pressed = ImGui::Button(selected ? picked : label, ImVec2(w, kModeHeight));
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(5);
        return pressed;
    };
    if (lmbda_choice(ICON_FA_USERS "  Host a session##mode", ICON_FA_CHECK "  Host a session##mode", mAction == Action::Host)) {
        mAction = Action::Host;
        mSettings.connect.hostMode = true;
        SaveSettings_cai();
    }
    ImGui::SameLine();
    if (lmbda_choice(ICON_FA_SIGN_IN_ALT "  Join a flight##modejoin", ICON_FA_CHECK "  Join a flight##modejoin",
                     mAction == Action::Join)) {
        mAction = Action::Join;
        mSettings.connect.hostMode = false;
        SaveSettings_cai();
    }
}

// The recent hosts under the address field (0.9.21: a list instead of a row of buttons that got cut).
void ConnectPage::ui_recent_list_cai(const ImVec2& pos, float width)
{
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, 400.0f));
    if (!ImGui::BeginPopup("##recentList"))
        return;
    for (std::size_t i = 0; i < mSettings.connect.recent.size(); ++i) {
        const std::string& address = mSettings.connect.recent[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(address.c_str(), address == mJoinAddress, ImGuiSelectableFlags_None, ImVec2(0.0f, kRowButtonH)))
            mJoinAddress = address;
        ImGui::PopID();
    }
    ImGui::EndPopup();
}

// The join form.
void ConnectPage::ui_join_form_cai()
{
    ui_form_header_cai("Connect to the host");
    ui_label_cai("Host address");
    const bool hasRecent = !mSettings.connect.recent.empty();
    ImGui::SetNextItemWidth(hasRecent ? ImGui::GetContentRegionAvail().x - 40.0f : -1.0f);
    ImGui::InputTextWithHint("##address", "IP or name, e.g. 192.168.1.20", &mJoinAddress);
    if (hasRecent) {
        const ImVec2 fieldMin = ImGui::GetItemRectMin();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CHEVRON_DOWN "##recent", ImVec2(32.0f, 0.0f)))
            ImGui::OpenPopup("##recentList");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Recent hosts");
        ui_recent_list_cai(ImVec2(fieldMin.x, ImGui::GetItemRectMax().y), ImGui::GetItemRectMax().x - fieldMin.x);
    }
    ui_label_cai("Port");
    if (ui_port_input_cai("##joinport", mSettings.connect.joinPort))
        SaveSettings_cai();
    ImGui::SameLine();
    ImGui::TextDisabled("default %u", static_cast<unsigned>(net::kDefaultPort));
    ui_label_cai("Password");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 40.0f);
    ImGui::InputTextWithHint("##joinpw", "if the host set one", &mJoinPassword,
                             mShowPassword ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    if (ImGui::Button(mShowPassword ? ICON_FA_EYE_SLASH "##showjoin" : ICON_FA_EYE "##showjoin", ImVec2(32.0f, 0.0f)))
        mShowPassword = !mShowPassword;
    std::string why;
    const bool can = CanJoin_cai(why);
    // "##connect": the same ID for both texts.
    const char* label = mHub.Session_cai().State_cai() == net::SessionState::Idle ? ICON_FA_PLUG "  Connect to host##connect"
                                                                                  : ICON_FA_REDO "  Connect again##connect";
    if (ui::ui_button_cai(label, ImVec2(-1.0f, kButtonHeight), can))
        DoJoin_cai();
    if (!can)
        ui::ui_wrapped_text_cai(why, &ui::theme::kChangedText);
    else
        ui::ui_wrapped_text_cai("Uses the host's analysis; yours is not changed.",
                                &ui::theme::kTextDim);
}

// The host form.
void ConnectPage::ui_host_form_cai()
{
    ui_form_header_cai("Host setup");
    ui_label_cai("UDP port");
    if (ui_port_input_cai("##hostport", mSettings.connect.hostPort))
        SaveSettings_cai();
    ImGui::SameLine();
    ImGui::TextDisabled("default %u", static_cast<unsigned>(net::kDefaultPort));
    ui_label_cai("Password");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 40.0f);
    ImGui::InputTextWithHint("##hostpw", "empty = no password", &mHostPassword,
                             mShowPassword ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    if (ImGui::Button(mShowPassword ? ICON_FA_EYE_SLASH "##showhost" : ICON_FA_EYE "##showhost", ImVec2(32.0f, 0.0f)))
        mShowPassword = !mShowPassword;
    ui_label_cai("Max people");
    ImGui::SetNextItemWidth(110.0f);
    static constexpr const char* kMaxItems[] = {"2", "3", "4"};
    int index = std::clamp(mSettings.connect.maxPeople, 2, net::kMaxPeople) - 2;
    if (ImGui::Combo("##max", &index, kMaxItems, IM_ARRAYSIZE(kMaxItems))) {
        mSettings.connect.maxPeople = index + 2;
        SaveSettings_cai();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("you included");
    ui_label_cai("Joiners start");
    ImGui::SetNextItemWidth(-1.0f);
    static constexpr const char* kStartItems[] = {"as Crew (automatic)", "as Observer (I assign seats)"};
    int start = mSettings.connect.joinersObserve ? 1 : 0;
    if (ImGui::Combo("##start", &start, kStartItems, IM_ARRAYSIZE(kStartItems))) {
        mSettings.connect.joinersObserve = start == 1;
        SaveSettings_cai();
    }
    std::string why;
    const bool can = CanHost_cai(why);
    if (ui::ui_button_cai(ICON_FA_USERS "  Start hosting##host", ImVec2(-1.0f, kButtonHeight), can))
        DoHost_cai();
    if (!can)
        ui::ui_wrapped_text_cai(why, &ui::theme::kChangedText);
}

// Connecting: progress and Cancel.
void ConnectPage::ui_connecting_cai()
{
    const net::NetSession& session = mHub.Session_cai();
    const long long seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - mStartedAt).count();
    static constexpr const char* kDots[] = {"", ".", "..", "..."};
    const char* dots = kDots[static_cast<int>(ImGui::GetTime() * 2.0) % 4];
    ImGui::SeparatorText("Joining");
    ImGui::TextUnformatted(session.State_cai() == net::SessionState::Connecting
                               ? fmt::format("Connecting to {}:{}{}", mJoinAddress, mSettings.connect.joinPort, dots).c_str()
                               : fmt::format("Connected - joining the session{}", dots).c_str());
    ImGui::TextDisabled("%lld s (gives up after %.0f s)", seconds, net::kConnectTimeout);
    ImGui::Spacing();
    if (ui::ui_color_button_cai(ICON_FA_TIMES "  Cancel", ImVec2(-1.0f, kButtonHeight), ui::theme::kError))
        mHub.Leave_cai("cancelled");
}

// My seat: the aircraft's seats and "back to my seat"; in VR the view slides there (0.9.25).
void ConnectPage::ui_my_seat_cai()
{
    if (!mSeats)
        return;
    ui_label_cai("My seat", ui::SeatRowHelp_cai());
    if (ui::ui_seat_row_cai(*mSeats, mSettings.alerts))
        SaveSettings_cai();
}

// In a session: summary, notices, addresses.
void ConnectPage::ui_in_session_cai()
{
    net::NetSession& session = mHub.Session_cai();
    ImGui::SeparatorText(session.IsHost_cai() ? "You are hosting" : "You joined");
    ImGui::TextUnformatted(fmt::format(ICON_FA_USER "  {}", session.Local_cai().name).c_str());
    if (session.IsHost_cai()) {
        const net::HostSettings& host = session.HostSettings_cai();
        ui::ui_wrapped_text_cai(fmt::format("Port {} - password {} - up to {} people", host.port,
                                            host.password.empty() ? "none" : "set", host.maxPeople),
                                &ui::theme::kTextDim);
        // Can change while hosting: e.g. the first joiner as crew, everybody after as observers.
        ui_label_cai("Joiners start", "For the people who join from now on. The people already in keep their role "
                                      "(change it in the people list).");
        ImGui::SetNextItemWidth(-1.0f);
        static constexpr const char* kStartItems[] = {"as Crew (automatic)", "as Observer (I assign seats)"};
        int start = host.joinersStartAs == net::Role::Observer ? 1 : 0;
        if (ImGui::Combo("##startlive", &start, kStartItems, IM_ARRAYSIZE(kStartItems))) {
            std::string why;
            const net::Role role = start == 1 ? net::Role::Observer : net::Role::Crew;
            Report_cai(session.SetJoinersStartAs_cai(role, why), why);
            mSettings.connect.joinersObserve = start == 1;
            SaveSettings_cai();
        }

    } else {
        const net::Person* me = session.Find_cai(session.MyId_cai());
        ui::ui_wrapped_text_cai(fmt::format("{}'s session at {}:{} - your seat: {}", session.NameOf_cai(net::kHostId),
                                            session.JoinSettings_cai().address, session.JoinSettings_cai().port,
                                            me ? net::RoleName_cai(me->role) : "-"),
                                &ui::theme::kTextDim);
    }
    ui_my_seat_cai();
    ImGui::Spacing();
    ImGui::SeparatorText("Message to everybody");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
    const bool enter = ImGui::InputTextWithHint("##notice", "e.g. ready for engine start", &mNotice,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ui::ui_button_cai(ICON_FA_PAPER_PLANE " Send", ImVec2(-1.0f, 0.0f), !mNotice.empty()) || enter) && !mNotice.empty()) {
        std::string why;
        Report_cai(session.SendNotice_cai(mNotice, why), why);
        mNotice.clear();
    }
    ImGui::Spacing();
    if (!session.IsHost_cai())
        ui::ui_wrapped_text_cai("Closing this window keeps you in the session. Leave is at the bottom.", &ui::theme::kTextDim);
}


// The right column.
void ConnectPage::ui_right_cai(float width, float height)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::theme::kPopupBg);
    ImGui::BeginChild("##right", ImVec2(width, height), ImGuiChildFlags_Borders);
    ui_people_cai();
    ui_control_buttons_cai();
    ImGui::Spacing();
    ui_checks_cai();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// The people table.
void ConnectPage::ui_people_cai()
{
    const net::NetSession& session = mHub.Session_cai();
    const std::vector<net::Person>& people = session.People_cai();
    const int max = session.IsHost_cai() ? session.HostSettings_cai().maxPeople : net::kMaxPeople;
    ui::ui_separator_help_cai(session.Active_cai() ? fmt::format("People  {} of {}", people.size(), max).c_str() : "People",
        "Prio: who wins when two people move the same control within the conflict time: the host, then the PIC, then "
        "this order (the host changes it with the arrows once two or more have joined).\n"
        "Name: yours is in colour; the crown is the master (the host: assigns roles, seats and levers, can take control). "
        "A triangle is a warning.\n"
        "Seat: Crew works the shared controls, an Observer only watches; a chair: they sit in one of the seats (only their view).\n"
        "Control: PIC = pilot in command, who flies. For Crew, the levers button: the levers the host gave them.\n"
        "Actions: Offer control (the PIC), make Observer / Crew and Remove (the host).");
    if (!session.Active_cai() || people.empty()) {
        ImGui::TextDisabled("Nobody yet: host a session or join one.");
        return;
    }
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("##people", 6, flags))
        return;
    const std::vector<net::SeatInfo>& seats = session.SessionSeats_cai();
    ImGui::TableSetupColumn("Prio", ImGuiTableColumnFlags_WidthFixed, CanReorder_cai(people) ? 60.0f : 34.0f);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Seat", ImGuiTableColumnFlags_WidthFixed, 66.0f);
    ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, 54.0f);   // PIC, or the levers button
    ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 44.0f);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 132.0f);
    ImGui::TableHeadersRow();
    for (const net::Person& p : people) {
        ImGui::PushID(p.id);
        ImGui::TableNextRow(ImGuiTableRowFlags_None, kRowButtonH + 4.0f);
        ImGui::TableNextColumn();
        ui_priority_cai(p, people);
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        const bool me = p.id == session.MyId_cai();
        // Your own name in colour; "(you)" only where it fits (0.9.21: it was cut).
        if (me)
            ImGui::TextColored(ui::theme::kAqua, "%s", p.name.c_str());
        else
            ImGui::TextUnformatted(p.name.c_str());
        if (me && ImGui::IsItemHovered())
            ImGui::SetTooltip("That's you");
        if (p.master) {
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextUnformatted(ICON_FA_CROWN);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The master: hosts the session, assigns seats, can take control.");
        }
        // The cell's visible edge is its clip rect (the content region runs past it in a table).
        const float youX = ImGui::GetItemRectMax().x + 6.0f;
        if (me && youX + ImGui::CalcTextSize("(you)").x <= ImGui::GetWindowDrawList()->GetClipRectMax().x) {
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextDisabled("(you)");
        }
        if (const std::string warn = FlagsText_cai(p.flags); !warn.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ui::theme::kChangedText, ICON_FA_EXCLAMATION_TRIANGLE);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", warn.c_str());
        }
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (p.role == net::Role::Observer)
            ImGui::TextColored(ui::theme::kTextDim, ICON_FA_EYE " Obs.");
        else
            ImGui::TextUnformatted("Crew");
        // Where they sit in the cockpit (the host's seats, 0.9.6): a chair, the name in the tooltip.
        if (p.seat < seats.size()) {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(ui::theme::kSavedText, ICON_FA_CHAIR);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s sits in the %s (only their own view; pick yours under My seat or on the Flight deck)", p.name.c_str(),
                                  seats[p.seat].name.c_str());
        }
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (p.pic) {
            ImGui::TextColored(ui::theme::kSavedText, ICON_FA_PLANE " PIC");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("PIC = pilot in command: the one who has the controls.\n"
                                  "Wins conflicts after the host; the others move to the PIC's position.");
        } else {
            ui_levers_button_cai(p);               // the levers this Crew member works (0.9.12)
        }
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (p.id == net::kHostId)
            ImGui::TextDisabled("host");
        else
            ImGui::TextUnformatted(fmt::format("{} ms", p.pingMs).c_str());
        ImGui::TableNextColumn();
        ui_person_actions_cai(p);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// The priority of one row: its number, and ▲▼ for the master (the host stays first).
void ConnectPage::ui_priority_cai(const net::Person& p, const std::vector<net::Person>& people)
{
    net::NetSession& session = mHub.Session_cai();
    const int prio = session.PriorityOf_cai(p.id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(prio > 0 ? fmt::format("{}", prio).c_str() : "-");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Who wins when two people move the same control within 1 second:\n"
                          "the host, then the pilot in command, then this order (lower first)");
    if (p.id == net::kHostId || !CanReorder_cai(people))
        return;
    int last = 0;
    for (const net::Person& other : people)
        last = std::max(last, session.PriorityOf_cai(other.id));
    std::string why;
    const ImVec2 size(20.0f, kRowButtonH);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
    ImGui::SameLine(0.0f, 4.0f);
    if (ui::ui_button_cai(ICON_FA_CARET_UP "##up", size, prio > 2))
        Report_cai(session.MovePriority_cai(p.id, -1, why), why);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Move %s up (wins more conflicts)", p.name.c_str());
    ImGui::SameLine(0.0f, 2.0f);
    if (ui::ui_button_cai(ICON_FA_CARET_DOWN "##down", size, prio > 0 && prio < last))
        Report_cai(session.MovePriority_cai(p.id, +1, why), why);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Move %s down", p.name.c_str());
    ImGui::PopStyleVar();
}

// The master can reorder only with two or more joiners (the host always stays first).
bool ConnectPage::CanReorder_cai(const std::vector<net::Person>& people) const
{
    if (!mHub.Session_cai().IsHost_cai())
        return false;
    auto lmbda_joiner = [](const net::Person& p) { return p.id != net::kHostId; };
    return std::count_if(people.begin(), people.end(), lmbda_joiner) >= 2;
}

// The levers of one Crew member: an icon (lit when they work some); the master opens the list.
void ConnectPage::ui_levers_button_cai(const net::Person& p)
{
    net::NetSession& session = mHub.Session_cai();
    const bool canSet = session.IsHost_cai() && p.role == net::Role::Crew;
    const std::string label = p.levers ? fmt::format(ICON_FA_SLIDERS_H " {}##levers", std::popcount(p.levers))
                                       : std::string(ICON_FA_SLIDERS_H "##levers");
    if (p.levers)
        ImGui::PushStyleColor(ImGuiCol_Text, ui::theme::kSavedText);
    if (ui::ui_button_cai(label.c_str(), ImVec2(44.0f, kRowButtonH), canSet) || (mOpenLevers && mLeversFor == p.id && canSet)) {
        mLeversFor = p.id;
        mOpenLevers = false;
        ImGui::OpenPopup("##leversPopup");
    }
    if (p.levers)
        ImGui::PopStyleColor();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        const std::string works = p.levers ? fmt::format("{} works the {}.", p.name, net::LeverNames_cai(p.levers))
                                           : fmt::format("{} works no levers (the PIC works them all).", p.name);
        ImGui::SetTooltip("%s\n%s", works.c_str(),
                          canSet ? "Press: choose the levers they work (throttle, flaps, ...)."
                                 : (session.IsHost_cai() ? "Observers work no levers: make them Crew first."
                                                         : "The master (the host) hands out the levers."));
    }
    ui_levers_popup_cai();
}

// The list of levers for one Crew member (the master): a box each; one owner per lever.
void ConnectPage::ui_levers_popup_cai()
{
    if (!ImGui::BeginPopup("##leversPopup"))
        return;
    net::NetSession& session = mHub.Session_cai();
    const net::Person* who = session.Find_cai(mLeversFor);
    if (!who || who->pic || who->role != net::Role::Crew) {
        ImGui::TextDisabled("Only for a Crew member who is not the PIC.");
        ImGui::EndPopup();
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(fmt::format(ICON_FA_SLIDERS_H "  Levers {} works", who->name).c_str());
    // Hide, at the top right (0.9.22): VR users need a target, not a click outside.
    const float side = ImGui::GetFrameHeight();
    const float right = ImGui::GetWindowContentRegionMax().x;
    ImGui::SameLine(std::max(ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + 12.0f, right - side));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
    if (ui::ui_button_cai(ICON_FA_TIMES "##leversHide", ImVec2(side, side)))
        ImGui::CloseCurrentPopup();
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hide (changes are already applied)");
    ImGui::Separator();
    std::uint16_t levers = who->levers;
    bool changed = false;
    for (int i = 0; i < net::kLeverCount; ++i) {
        const std::uint16_t bit = static_cast<std::uint16_t>(1u << i);
        bool on = (levers & bit) != 0;
        std::string owner;                                   // somebody else works it now
        for (const net::Person& other : session.People_cai())
            if (other.id != who->id && !other.pic && (other.levers & bit))
                owner = other.name;
        const std::string label = fmt::format("{}{}##lever{}", net::LeverName_cai(bit), owner.empty() ? "" : "   (now " + owner + ")", i);
        if (ImGui::Checkbox(label.c_str(), &on)) {
            levers = static_cast<std::uint16_t>(on ? (levers | bit) : (levers & ~bit));
            changed = true;
        }
    }
    if (changed) {
        std::string why;
        Report_cai(session.SetLevers_cai(who->id, levers, why), why);
    }
    ImGui::Separator();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 300.0f);
    ImGui::TextDisabled("They work these with their own hardware or mouse; everybody, the PIC too, follows. "
                        "The yoke, rudder and toe brakes always stay with the PIC. Changes apply at once.");
    ImGui::PopTextWrapPos();
    ImGui::EndPopup();
}

// The action buttons of one row (always visible; disabled when they do not apply).
void ConnectPage::ui_person_actions_cai(const net::Person& p)
{
    net::NetSession& session = mHub.Session_cai();
    const bool me = p.id == session.MyId_cai();
    const bool iAmPic = session.PicId_cai() == session.MyId_cai();
    const bool master = session.IsHost_cai();
    std::string why;
    // Offer: only the PIC, to another crew member.
    const bool canOffer = iAmPic && !me && p.role == net::Role::Crew;
    if (ui::ui_button_cai("Offer", ImVec2(52.0f, kRowButtonH), canOffer))
        Report_cai(session.OfferControl_cai(p.id, why), why);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(canOffer ? "Offer control to %s (they answer yes / no)"
                                   : "Only the pilot in command offers control, to a crew member (not %s)",
                          p.name.c_str());
    ImGui::SameLine();
    // Seat: only the master, not for the host itself.
    const bool canSeat = master && p.id != net::kHostId;
    const bool observer = p.role == net::Role::Observer;
    if (ui::ui_button_cai(observer ? ICON_FA_USER "##seat" : ICON_FA_EYE "##seat", ImVec2(32.0f, kRowButtonH), canSeat))
        Report_cai(session.SetRole_cai(p.id, observer ? net::Role::Crew : net::Role::Observer, why), why);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(canSeat ? (observer ? "Make %s crew (can work the shared controls)"
                                              : "Make %s an observer (sees, touches nothing)")
                                  : "Only the master (the host) assigns seats", p.name.c_str());
    ImGui::SameLine();
    if (ui::ui_color_button_cai(ICON_FA_USER_MINUS "##remove", ImVec2(32.0f, kRowButtonH), ui::theme::kError, canSeat)) {
        mRemoveId = p.id;
        mAskRemove = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(canSeat ? "Remove %s from the session" : "Only the master (the host) removes people", p.name.c_str());
}

// Ask for control / Take control.
void ConnectPage::ui_control_buttons_cai()
{
    net::NetSession& session = mHub.Session_cai();
    if (!session.Active_cai())
        return;
    const net::Person* me = session.Find_cai(session.MyId_cai());
    const bool iAmPic = session.PicId_cai() == session.MyId_cai();
    const bool crew = me && me->role == net::Role::Crew;
    std::string why;
    ImGui::Spacing();
    const float w = session.IsHost_cai() ? (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f : -1.0f;
    if (ui::ui_button_cai(ICON_FA_HAND_PAPER "  Ask for control", ImVec2(w, 34.0f), !iAmPic && crew))
        Report_cai(session.RequestControl_cai(why), why);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", iAmPic ? "You are the pilot in command" : !crew ? "Observers cannot take control"
                                                                               : "The pilot in command answers yes / no");
    if (session.IsHost_cai()) {
        ImGui::SameLine();
        if (ui::ui_color_button_cai(ICON_FA_CROWN "  Take control", ImVec2(-1.0f, 34.0f), ui::theme::kChanged, !iAmPic))
            Report_cai(session.TakeControl_cai(why), why);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("The master takes control at once, without asking.");
    }
    ImGui::TextDisabled("Pilot in command: %s", iAmPic ? "you" : session.NameOf_cai(session.PicId_cai()).c_str());
}

// "Before you start" / "Before you fly".
void ConnectPage::ui_checks_cai()
{
    const net::NetSession& session = mHub.Session_cai();
    const auto loaded = xp::GetUserAircraft_cai();
    if (!session.Active_cai()) {
        ImGui::SeparatorText("Before you start");
        ui_check_line_cai(loaded ? Mark::Ok : Mark::Bad,
                          loaded ? fmt::format("Aircraft loaded: {}", loaded->displayName)
                                 : std::string("Load an aircraft in X-Plane"));
        const auto& data = mAnalysis.Data_cai();
        if (AnalysisReady_cai())
            ui_check_line_cai(Mark::Ok, fmt::format("Analysis saved - sharing {} of {} controls", data->SharedCount_cai(),
                                                    data->ShareableCount_cai()));
        else if (mAnalysis.Dirty_cai())
            ui_check_line_cai(Mark::Warn, "The Analyse page has unsaved changes: save them first (hosting sends the saved file)");
        else if (mAnalysis.HasAircraft_cai() && !mAnalysis.IsLoadedAircraft_cai())
            ui_check_line_cai(Mark::Warn, "The Analyse page shows another aircraft: open Home to switch back");
        else
            ui_check_line_cai(Mark::Warn, "No analysis: you can join (the host's is used) but not host");
        if (data && AnalysisReady_cai() && data->SharedCount_cai() == 0)
            ui_check_line_cai(Mark::Warn, "Nothing is ticked for sharing on the Analyse page");
        ui_check_line_cai(Utils::Trim_cai(mSettings.connect.name).empty() ? Mark::Bad : Mark::Ok,
                          Utils::Trim_cai(mSettings.connect.name).empty() ? "Enter your name" : "Name set");
        ui_check_line_cai(Mark::Info, "Everybody must fly the same aircraft (the same .acf file name)");
        return;
    }
    ImGui::SeparatorText("Before you fly");
    ui_check_line_cai(Mark::Ok, fmt::format("Same aircraft for everybody: {}", session.Local_cai().acfName));
    if (session.IsHost_cai())
        ui_check_line_cai(Mark::Ok, "Your analysis is used by everybody");
    else if (session.HostProfile_cai())
        ui_check_line_cai(Mark::Info, "Using the host's analysis (in memory only; your file is not changed)");
    else
        ui_check_line_cai(Mark::Ok, "Same analysis as the host");
    bool warned = false;
    for (const net::Person& p : session.People_cai()) {
        if (p.flags & net::kFlagAcfVersion) {
            ui_check_line_cai(Mark::Warn, fmt::format("{}: another version of the aircraft - some controls may not match",
                                                      p.id == session.MyId_cai() ? "You" : p.name));
            warned = true;
        }
        if (p.flags & net::kFlagMissingControls) {
            ui_check_line_cai(Mark::Warn, fmt::format("{}: some shared controls are missing in that aircraft",
                                                      p.id == session.MyId_cai() ? "You" : p.name));
            warned = true;
        }
    }
    if (!warned && session.People_cai().size() > 1)
        ui_check_line_cai(Mark::Ok, "No aircraft differences found");
    if (session.People_cai().size() <= 1)
        ui_check_line_cai(Mark::Info, "Waiting for the others to join");
    ui_position_cai();
    ui_sharing_cai();
}

// Position and weather lines of "Before you fly".
void ConnectPage::ui_position_cai()
{
    const net::NetSession& session = mHub.Session_cai();
    const net::FlightStats& flight = mHub.Flight_cai();
    if (session.People_cai().size() > 1 && flight.running) {
        auto lmbda_status = [this](const net::StatusText& line) {
            if (line.text.empty())
                return;
            using StatusMark = net::StatusText::Mark;
            ui_check_line_cai(line.mark == StatusMark::Ok ? Mark::Ok : line.mark == StatusMark::Warn ? Mark::Warn : Mark::Info, line.text,
                              line.tooltip);
        };
        if (flight.traceWriting)
            ui_check_line_cai(Mark::Info, fmt::format("Flight trace on ({:.0f} KB) - Home > About switches it off", flight.traceBytes / 1024.0));
        lmbda_status(net::FollowLine_cai(session, flight));
        lmbda_status(net::LoadLine_cai(session, flight));
        if (flight.iAmPic) {
            ui_check_line_cai(Mark::Ok, fmt::format("Flight: your yoke, pedals and levers go to everybody ({} engine{})", flight.engines,
                                                    flight.engines == 1 ? "" : "s"));
        } else if (flight.inputsAt == std::chrono::steady_clock::time_point{}) {
            ui_check_line_cai(Mark::Info, "Flight: waiting for the PIC's yoke, pedals and levers");
        } else {
            const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - flight.inputsAt).count();
            ui_check_line_cai(ms < 3000 ? Mark::Ok : Mark::Warn,
                              fmt::format("Flight: {}'s yoke, pedals and levers {}{}", session.NameOf_cai(session.PicId_cai()),
                                          session.Rules_cai().flight == net::FlightMode::Follow ? "move yours" : "fly your aircraft",
                                          ms < 1500 ? std::string() : fmt::format(" (nothing new for {} s)", ms / 1000)),
                              "Your own joystick and pedals are switched off while you are not the PIC. Take control to fly yourself.");
        }
        if (!flight.iAmPic && flight.heldPerSecond > 0)
            ui_check_line_cai(Mark::Warn, fmt::format("A hardware lever here moves the levers ({} times a second) - held at the PIC's", flight.heldPerSecond));
        if (!flight.missing.empty()) {
            ui_check_line_cai(Mark::Warn, fmt::format("Flight: {} controls not in this X-Plane", flight.missing.size()),
                              ListText_cai("Not found:", flight.missing));
        }
        if (session.Rules_cai().copilotAxes) {
            const std::string help = "The copilot binds their hardware to X-Plane's Copilot Pitch / Roll / Yaw axes. "
                                     "Linked controls: the larger deflection wins. Fly-by-wire sticks: both are added. "
                                     "Two people can fly at once - agree who flies.";
            if (!flight.copilotAvailable)
                ui_check_line_cai(Mark::Warn, "Copilot axes: this X-Plane has no copilot side datarefs (needs a newer X-Plane 12)", help);
            else if (flight.iAmCopilot)
                ui_check_line_cai(Mark::Warn, "Copilot axes: your stick / pedals fly the copilot side - for everybody", help);
            else if (flight.copilotId != net::kNoId)
                ui_check_line_cai(Mark::Warn, fmt::format("Copilot axes: {} is on the copilot side", session.NameOf_cai(flight.copilotId)), help);
            else
                ui_check_line_cai(Mark::Info, "Copilot axes: on - nobody on the copilot side now", help);
        }
        if (flight.picPaused)
            ui_check_line_cai(Mark::Info, flight.iAmPic ? "Paused - by you (everybody is paused)" : "Paused by the PIC");
    }
    net::PositionStats& pos = mHub.Position_cai();
    using Move = net::PositionStats::Move;
    if (session.People_cai().size() > 1) {
        if (pos.move != Move::None)
            ui_check_line_cai(Mark::Info, pos.move == Move::Moving ? "Position: moving to the PIC..."
                                                                   : "Position: new flight at the PIC's position, loading...");
        else if (pos.iAmPic)
            ui_check_line_cai(Mark::Info, "Position: you are the PIC - pilot in command (the others move to you)");
        else if (!pos.known)
            ui_check_line_cai(Mark::Info, "Position: waiting for the PIC's position");
        else if (pos.off) {
            if (ui_check_button_line_cai(Mark::Warn, fmt::format("Position: {} from the PIC", Utils::DistanceText_cai(pos.distanceM)),
                                         ICON_FA_MAP_MARKER_ALT " Move to PIC", OffText_cai(pos) +
                                         (pos.enginesDiffer ? (pos.picEngines ? " (the PIC's run, yours are off)" : " (yours run, the PIC's are off)")
                                                            : std::string())))
                pos.moveRequested = true;
        } else
            ui_check_line_cai(Mark::Ok, fmt::format("Position: {} from the PIC", Utils::DistanceText_cai(pos.distanceM)));
    }
    const long long ago = pos.weatherAt == std::chrono::steady_clock::time_point{} ? -1
        : std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - pos.weatherAt).count();
    const std::string when = ago < 0 ? std::string("not yet") : fmt::format("{} s ago", ago);
    const std::string skipped = pos.weatherSkipped.empty() ? std::string() : ListText_cai("Skipped here:", pos.weatherSkipped);
    if (pos.weatherSending)
        ui_check_line_cai(pos.weatherSkipped.empty() ? Mark::Ok : Mark::Warn,
                          fmt::format("Weather and time: sent to everybody ({} values, {})", pos.weatherValues, when), skipped);
    else if (pos.weatherFollowing)
        ui_check_line_cai(pos.weatherSkipped.empty() ? Mark::Ok : Mark::Warn,
                          fmt::format("Weather and time: following the host (updated {})", when), skipped);
    else
        ui_check_line_cai(Mark::Info, "Weather and time: your own ([position] weather=0)");
}

// A check line with a button at the right end. True when pressed.
bool ConnectPage::ui_check_button_line_cai(Mark mark, const std::string& text, const char* button, const std::string& tooltip)
{
    const float buttonW = ImGui::CalcTextSize(button).x + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f;
    const float iconCell = ImGui::GetFontSize() + 8.0f;
    const float x = ImGui::GetCursorPosX();
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(mark == Mark::Warn ? ui::theme::kChangedText : ui::theme::kTextDim, "%s",
                       mark == Mark::Warn ? ICON_FA_EXCLAMATION_TRIANGLE : ICON_FA_INFO_CIRCLE);
    ImGui::SameLine(x + iconCell);
    ui::ui_text_ellipsis_cai(text, avail - iconCell - buttonW - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip.empty() ? text.c_str() : tooltip.c_str());
    ImGui::SameLine(x + avail - buttonW);
    return ui::ui_button_cai(button, ImVec2(buttonW, 0.0f));
}

// The "Sharing" box: what the control sharing does (commands and values).
void ConnectPage::ui_sharing_cai()
{
    const net::SyncStats& sync = mHub.Sync_cai();
    ImGui::SeparatorText("Sharing");
    if (!sync.running) {
        ui_check_line_cai(Mark::Info, "Starts with the session");
        return;
    }
    const std::size_t cmdMissing = sync.commands - sync.commandsOk;
    ui_check_line_cai(cmdMissing == 0 ? Mark::Ok : Mark::Warn,
                      fmt::format("{} shared commands - {} found here{}", sync.commands, sync.commandsOk,
                                  cmdMissing == 0 ? std::string() : fmt::format(", {} missing", cmdMissing)),
                      sync.missing.empty() ? std::string() : ListText_cai("Not in this X-Plane (skipped here):", sync.missing));
    const std::size_t refSkipped = sync.datarefs - sync.datarefsOk;
    ui_check_line_cai(refSkipped == 0 ? Mark::Ok : Mark::Warn,
                      fmt::format("{} shared values (switches, knobs, levers) - {} usable here{}", sync.datarefs,
                                  sync.datarefsOk, refSkipped == 0 ? std::string() : fmt::format(", {} skipped", refSkipped)),
                      sync.readOnly.empty() ? std::string() : ListText_cai("Read-only or another type here (skipped):", sync.readOnly));
    if (log::IsEnabled_cai(log::Level::Debug)) {
        for (const std::string& name : sync.missing)
            ui_check_line_cai(Mark::Warn, fmt::format("missing: {}", name));
        for (const std::string& name : sync.readOnly)
            ui_check_line_cai(Mark::Warn, fmt::format("read-only: {}", name));
    }
    const long long ago = sync.last.empty() ? 0 : std::chrono::duration_cast<std::chrono::seconds>(
                                                      std::chrono::steady_clock::now() - sync.lastAt).count();
    ui_check_line_cai(Mark::Info, sync.last.empty() ? std::string("No change yet")
                                                    : fmt::format("Last: {} - {} s ago", sync.last, ago));
    ui_check_line_cai(sync.rejected > 0 ? Mark::Warn : Mark::Info,
                      fmt::format("{} a minute - {} sent, {} from others, {} not shared, {} blocked", sync.perMinute,
                                  sync.sent, sync.applied, sync.rejected, sync.blocked));
}

// "title\n a\n b\n ... and N more" for a tooltip (at most 20 names).
std::string ConnectPage::ListText_cai(const char* title, const std::vector<std::string>& names)
{
    std::string text = title;
    for (std::size_t i = 0; i < names.size() && i < 20; ++i)
        text += "\n" + names[i];
    if (names.size() > 20)
        text += fmt::format("\n... and {} more", names.size() - 20);
    return text;
}

// One check line.
void ConnectPage::ui_check_line_cai(Mark mark, const std::string& text, const std::string& help)
{
    const char* icon = ICON_FA_INFO_CIRCLE;
    const ImVec4* color = &ui::theme::kTextDim;
    switch (mark) {
        case Mark::Ok:   icon = ICON_FA_CHECK;                color = &ui::theme::kSavedText;   break;
        case Mark::Warn: icon = ICON_FA_EXCLAMATION_TRIANGLE; color = &ui::theme::kChangedText; break;
        case Mark::Bad:  icon = ICON_FA_TIMES;                color = &ui::theme::kErrorText;   break;
        case Mark::Info: break;
    }
    // The icon in a fixed-width cell, so every text starts at the same place, with a gap.
    const float iconCell = ImGui::GetFontSize() + 8.0f;
    const float x = ImGui::GetCursorPosX();
    ImGui::TextColored(*color, "%s", icon);
    ImGui::SameLine(x + iconCell);
    if (help.empty()) {
        ui::ui_wrapped_text_cai(text);
        return;
    }
    // The details in a (?) at the end: clickable in VR (0.9.26, was a hover tooltip).
    const float marker = ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(40.0f, ImGui::GetContentRegionAvail().x - marker));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::SameLine();
    ui::ui_help_marker_cai(help.c_str());
}

// Status, buttons and the feed.
void ConnectPage::ui_bottom_cai()
{
    net::NetSession& session = mHub.Session_cai();
    const ImGuiStyle& style = ImGui::GetStyle();
    const bool active = session.State_cai() != net::SessionState::Idle &&
                        session.State_cai() != net::SessionState::Failed && session.State_cai() != net::SessionState::Lost;
    const char* leave = session.IsHost_cai() ? ICON_FA_SIGN_OUT_ALT "  End session" : ICON_FA_SIGN_OUT_ALT "  Leave";
    const char* deck = ICON_FA_DESKTOP "  Flight deck (hides this window)";
    const float leaveW = 150.0f;
    const float deckW = ImGui::CalcTextSize(deck).x + style.FramePadding.x * 2.0f + 8.0f;
    const float stripW = ImGui::GetContentRegionAvail().x - leaveW - deckW - style.ItemSpacing.x * 2.0f;
    ui::ui_status_strip_cai(fmt::format(ICON_FA_CIRCLE "  {}", net::StatusLine_cai(session)), StateColor_cai(session.State_cai()),
                            ui::theme::kTextOnColor, stripW, kButtonHeight);
    if (ImGui::IsItemHovered() && session.State_cai() != net::SessionState::Idle)
        ImGui::SetTooltip("%s", net::StatusLine_cai(session).c_str());
    ImGui::SameLine();
    if (ui::ui_color_button_cai(leave, ImVec2(leaveW, kButtonHeight), ui::theme::kError, active))
        mHub.Leave_cai(session.IsHost_cai() ? "you ended the session" : "you left the session");
    ImGui::SameLine();
    const bool vr = Globals::get_cai().inVr;
    const bool deckOk = static_cast<bool>(mOpenFlightDeck) && !vr;
    if (ui::ui_button_cai(deck, ImVec2(deckW, kButtonHeight), deckOk))
        mOpenFlightDeck();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", vr ? "In VR keep this window: the flight deck is for 2D flying."
                                   : mOpenFlightDeck ? "A small window for flying in 2D: state, pilot in command, requests.\n"
                                                       "Its status light brings this window back."
                                                     : "The flight deck window comes with the next build.");

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::theme::kInputBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, kFeedPadY));
    ImGui::BeginChild("##feed", ImVec2(0.0f, FeedHeight_cai()), ImGuiChildFlags_Borders);
    const auto& feed = mHub.Feed_cai();
    if (feed.empty())
        ImGui::TextDisabled("What happens in the session shows here.");
    for (const net::FeedLine& line : feed) {
        ImGui::TextDisabled("%s", line.time.c_str());
        ImGui::SameLine();
        ImGui::TextColored(FeedColor_cai(line.kind), "%s", line.text.c_str());
    }
    // New lines: scroll to the end now and next frame (the list's height is known one frame late).
    if (mSeenFeed != mHub.FeedRevision_cai()) {
        mSeenFeed = mHub.FeedRevision_cai();
        mScrollFeed = 2;
    }
    if (mScrollFeed > 0) {
        --mScrollFeed;
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// "Remove <name>?" confirmation.
void ConnectPage::ui_popup_remove_cai()
{
    ui::ui_popup_center_next_cai();
    if (!ImGui::BeginPopupModal(kPopupRemove, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        return;
    net::NetSession& session = mHub.Session_cai();
    ImGui::TextUnformatted(fmt::format("Remove {} from the session?", session.NameOf_cai(mRemoveId)).c_str());
    ImGui::TextDisabled("They can join again (with the password, if you set one).");
    ImGui::Spacing();
    if (ui::ui_color_button_cai(ICON_FA_USER_MINUS "  Remove", ImVec2(150.0f, 36.0f), ui::theme::kError)) {
        std::string why;
        Report_cai(session.Kick_cai(mRemoveId, why), why);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ui::ui_button_cai("Cancel", ImVec2(150.0f, 36.0f)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ======================================================================================
//  Actions
// ======================================================================================

// Starts hosting.
void ConnectPage::DoHost_cai()
{
    mEndedAt = {};                                  // the red header ends with a new try
    net::HostSettings host;
    host.port = mSettings.connect.hostPort;
    host.password = mHostPassword;
    host.maxPeople = mSettings.connect.maxPeople;
    host.joinersStartAs = mSettings.connect.joinersObserve ? net::Role::Observer : net::Role::Crew;
    SaveSettings_cai();
    net::NetSession& session = mHub.Session_cai();
    session.SetLocal_cai(MakeLocal_cai());
    LOG_INFO("Connect page: host on port {}", host.port);
    session.Host_cai(host);
    Refresh_cai();
}

// Joins.
void ConnectPage::DoJoin_cai()
{
    mEndedAt = {};
    net::JoinSettings join;
    std::string address(Utils::Trim_cai(mJoinAddress));
    // "address:port" also works.
    if (const std::size_t colon = address.rfind(':'); colon != std::string::npos && address.find(':') == colon) {
        int port = 0;
        if (Utils::ParseInt_cai(address.substr(colon + 1), port) && port > 0 && port < 65536) {
            mSettings.connect.joinPort = static_cast<std::uint16_t>(port);
            address = address.substr(0, colon);
        }
    }
    mJoinAddress = address;
    join.address = address;
    join.port = mSettings.connect.joinPort;
    join.password = mJoinPassword;
    mSettings.AddRecent_cai(address);
    SaveSettings_cai();
    net::NetSession& session = mHub.Session_cai();
    session.SetLocal_cai(MakeLocal_cai());
    LOG_INFO("Connect page: join {}:{}", join.address, join.port);
    mStartedAt = std::chrono::steady_clock::now();
    session.Join_cai(join);
}

// Who this simulator is.
net::LocalInfo ConnectPage::MakeLocal_cai() const
{
    net::LocalInfo info;
    info.name = std::string(Utils::Trim_cai(mSettings.connect.name));
    info.pluginVersion = fmt::format("{}.{}", info::kVersion, info::kBuild);
    if (const auto loaded = xp::GetUserAircraft_cai()) {
        info.acfName = Utils::Utf8_cai(loaded->acfFile.filename());
        // The content's hash: the same file on two computers gives the same stamp (dates differ).
        info.acfStamp = Utils::Hash_cai(Utils::ReadAll_cai(loaded->acfFile));
    }
    if (AnalysisReady_cai()) {
        const analysis::AnalysisData& data = *mAnalysis.Data_cai();
        info.profileText = Utils::ReadAll_cai(mAnalysis.AnalysisPath_cai());
        info.profileHash = analysis::ProfileHash_cai(data);
        info.allKeys = analysis::AllKeys_cai(data);
    }
    return info;
}

// True when this simulator can host.
bool ConnectPage::CanHost_cai(std::string& why) const
{
    if (Utils::Trim_cai(mSettings.connect.name).empty())
        why = "Enter your name first.";
    else if (!xp::GetUserAircraft_cai())
        why = "Load an aircraft in X-Plane first.";
    else if (!AnalysisReady_cai())
        why = mAnalysis.Dirty_cai() ? "Save the analysis first (Analyse page): the saved file is what everybody shares."
                                    : "Analyse the aircraft and save first: your analysis decides what everybody shares.";
    else if (mAnalysis.Data_cai()->SharedCount_cai() == 0)
        why = "Nothing is ticked for sharing on the Analyse page.";
    return why.empty();
}

// True when this simulator can join.
bool ConnectPage::CanJoin_cai(std::string& why) const
{
    if (Utils::Trim_cai(mSettings.connect.name).empty())
        why = "Enter your name first.";
    else if (!xp::GetUserAircraft_cai())
        why = "Load the same aircraft as the host first.";
    else if (Utils::Trim_cai(mJoinAddress).empty())
        why = "Enter the host's address.";
    return why.empty();
}

// True when the Analyse page holds the loaded aircraft's saved analysis.
bool ConnectPage::AnalysisReady_cai() const
{
    return mAnalysis.Data_cai().has_value() && mAnalysis.OnDisk_cai() && !mAnalysis.Dirty_cai() &&
           mAnalysis.IsLoadedAircraft_cai();
}

// Writes the settings file.
void ConnectPage::SaveSettings_cai()
{
    std::string error;
    mSettings.Save_cai(error);
}

// Reports an action that did not work.
void ConnectPage::Report_cai(bool ok, const std::string& why)
{
    if (!ok && !why.empty()) {
        LOG_INFO("Connect page: {}", why);
        mHub.Note_cai(net::SessionEvent::Kind::Declined, why);
    }
}

} // namespace sc::pages
