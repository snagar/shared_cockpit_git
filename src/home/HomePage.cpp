// shared_cockpit - the Home page: the steps of a shared flight

#include "home/HomePage.h"

#include <algorithm>
#include <vector>

#include "fmt/format.h"
#include "IconsFontAwesome5.h"

#include "shared/log/Log.h"
#include "shared/net/Protocol.h"
#include "Globals.h"
#include "SharedInfo.h"
#include "shared/ui/ShareCard.h"
#include "shared/ui/Widgets.h"
#include "shared/util/Libraries.h"
#include "shared/sync/SeatSync.h"
#include "shared/ui/SeatRow.h"
#include "shared/util/Utils.h"
#include "XPLMDefs.h"
#include "shared/xp/XPlane.h"

namespace sc::pages {

namespace {

constexpr float       kStepButtonHeight = 56.0f;     // large targets for VR controllers
constexpr float       kLeftColumnWidth  = 350.0f;
constexpr const char* kPopupAbout       = "About Shared Cockpit";

// "2026-11-22T19:41:00" -> "22 Nov 19:41".
std::string ShortTime_cai(const std::string& iso)
{
    static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int month = 0;
    if (iso.size() < 16 || !Utils::ParseInt_cai(iso.substr(5, 2), month) || month < 1 || month > 12)
        return iso;
    return fmt::format("{} {} {}", iso.substr(8, 2), kMonths[month - 1], iso.substr(11, 5));
}

} // namespace

HomePage::HomePage(analysis::Session& session, net::SessionHub& hub, settings::Settings& settings)
    : mSession(session), mHub(hub), mSettings(settings)
{
}

// Points the session at the loaded aircraft when it has no unsaved changes.
void HomePage::Refresh_cai()
{
    mAddresses = net::LocalAddresses_cai();
    mMachine = net::MachineName_cai();
    if (mSession.Dirty_cai() || mSession.Scanning_cai())
        return;
    const auto loaded = xp::GetUserAircraft_cai();
    if (!loaded) {
        mSession.Clear_cai();
        return;
    }
    if (mSession.AcfFile_cai() != loaded->acfFile || !mSession.IsLoadedAircraft_cai())
        mSession.SetAircraft_cai(loaded->acfFile, loaded->displayName, true);
}

// A big step button with its status line.
bool HomePage::ui_step_button_cai(int number, const char* label, bool done, bool next, bool enabled,
                                  const std::string& status, const ImVec4& statusColor)
{
    const std::string text = fmt::format("{}  {}##step{}", done ? ICON_FA_CHECK_CIRCLE : (next ? ICON_FA_ARROW_RIGHT : ICON_FA_CIRCLE),
                                         label, number);
    const ImVec4& color = !enabled ? ui::theme::kButtonOff : next ? ui::theme::kChanged : ui::theme::kButton;
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.04f, 0.5f));
    const bool pressed = ui::ui_color_button_cai(text.c_str(), ImVec2(-1.0f, kStepButtonHeight), color, enabled);
    ImGui::PopStyleVar();
    ui::ui_wrapped_text_cai(status, &statusColor);
    ImGui::Spacing();
    return pressed;
}

// Draws the page.
void HomePage::ui_draw_cai()
{
    const float top = ImGui::GetCursorPosY();
    ImGui::TextUnformatted("Select a step :");
    const char* about = ICON_FA_INFO_CIRCLE " About";
    const float aboutW = ImGui::CalcTextSize(about).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - aboutW);
    ImGui::SetCursorPosY(top - ImGui::GetStyle().FramePadding.y);
    if (ui::ui_button_cai(about))
        mAskAbout = true;
    ImGui::Separator();

    const float height = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##steps", ImVec2(kLeftColumnWidth, height));
    {
        const auto& data = mSession.Data_cai();
        const bool  analysed = data.has_value() && mSession.OnDisk_cai() && !mSession.Dirty_cai();
        std::string status;
        ImVec4      statusColor = ui::theme::kChangedText;
        if (!mSession.HasAircraft_cai())
            status = "No aircraft loaded.";
        else if (mSession.Scanning_cai())
            status = "Reading the aircraft files...";
        else if (mSession.Dirty_cai())
            status = "Not saved yet - open the step to save.";
        else if (!data)
            status = mSession.LastError_cai().empty() ? "Not analysed yet - start here." : mSession.LastError_cai();
        else {
            status = fmt::format("Analysed {} - sharing {} of {} controls", ShortTime_cai(data->analysed),
                                 data->SharedCount_cai(), data->ShareableCount_cai());
            statusColor = ui::theme::kSavedText;
            if (mSession.AircraftChanged_cai()) {
                status += " - the aircraft changed since, read it again";
                statusColor = ui::theme::kChangedText;
            }
        }
        if (ui_step_button_cai(1, "Analyse airplane & pick what to share", analysed, !analysed, true, status, statusColor) &&
            mOpenAnalyse)
            mOpenAnalyse();
        const net::NetSession& net = mHub.Session_cai();
        const bool inSession = net.Active_cai();
        const bool loaded = xp::GetUserAircraft_cai().has_value();
        std::string connectStatus;
        ImVec4 connectColor = ui::theme::kChangedText;
        if (inSession || net.State_cai() != net::SessionState::Idle) {
            connectStatus = net::StatusLine_cai(net);
            connectColor = inSession ? ui::theme::kSavedText
                         : (net.State_cai() == net::SessionState::Failed || net.State_cai() == net::SessionState::Lost)
                               ? ui::theme::kErrorText : ui::theme::kChangedText;
        } else if (!loaded) {
            connectStatus = "Load an aircraft first.";
        } else {
            connectStatus = analysed ? "Host a session, or join one." : "Join a session (hosting needs a saved analysis).";
            connectColor = ui::theme::kTextDim;
        }
        if (ui_step_button_cai(2, "Connect: host or join", inSession, analysed && !inSession, loaded || inSession,
                               connectStatus, connectColor) && mOpenConnect)
            mOpenConnect();
        // Step 3: the small 2D window for flying (hides this one). Not in VR.
        const bool vr = Globals::get_cai().inVr;
        const std::string deckStatus = vr ? std::string("In VR, keep this window (the flight deck is for 2D).")
                                     : inSession ? std::string("Open it to fly: hides this window; its light brings it back.")
                                                 : std::string("Works best once you are in a session.");
        if (ui_step_button_cai(3, "Flight deck (2D): fly with a small window", false, inSession && !vr, !vr, deckStatus,
                               vr ? ui::theme::kChangedText : ui::theme::kTextDim) && mOpenFlightDeck)
            mOpenFlightDeck();
        ui_seats_button_cai();
        // Donate, at the bottom of the steps (0.9.23).
        const float donateH = 36.0f;
        const float bottom = ImGui::GetWindowContentRegionMax().y - donateH;
        if (ImGui::GetCursorPosY() < bottom)
            ImGui::SetCursorPosY(bottom);
        ui::ui_donate_button_cai("home", ImVec2(140.0f, donateH));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (donateH - ImGui::GetFrameHeight()) * 0.5f);
        ImGui::TextDisabled("Free to use - thank you!");
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ui_right_column_cai(ImGui::GetContentRegionAvail().x, height);

    if (mAskAbout) {
        ImGui::OpenPopup(kPopupAbout);
        mAskAbout = false;
    }
    ui_popup_about_cai();
}

// "Seats": the small Seats window for changing seats (also alone, also in VR); the main window hides.
void HomePage::ui_seats_button_cai()
{
    const std::size_t count = mSeats ? mSeats->Seats_cai().size() : 0;
    std::string status = count == 0 ? std::string("No seats in this aircraft (vrconfig_helper can add them).")
                                    : fmt::format("Jump to one of {} seats in a small window (hides this one).", count);
    if (mSeats && count > 0 && mSeats->MySeat_cai() < count)
        status = fmt::format("You sit in the {}. Opens a small window (hides this one).", mSeats->Seats_cai()[mSeats->MySeat_cai()].name);
    const bool pressed = ui_step_button_cai(4, "Seats: pick where you sit", false, false, mSeats != nullptr,
                                            status, ui::theme::kTextDim);
    if (pressed && mOpenSeats)
        mOpenSeats();
}

// The right column: aircraft, shareability, session.
void HomePage::ui_right_column_cai(float width, float height)
{
    ImGui::BeginChild("##right", ImVec2(width, height));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::theme::kPanelBgDark);
    ImGui::BeginChild("##aircraft", ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 2.0f + 14.0f),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos(ImVec2(8.0f, 6.0f));
    ImGui::BeginGroup();
    if (mSession.HasAircraft_cai()) {
        ui::ui_text_ellipsis_cai(fmt::format(ICON_FA_PLANE "  {}", mSession.DisplayName_cai()), width - 20.0f);
        const std::string where = fmt::format("{} - {}", mSession.IsLoadedAircraft_cai() ? "Loaded aircraft" : "Other aircraft",
                                              Utils::Utf8_cai(mSession.AcfFile_cai().filename()));
        ui::ui_text_ellipsis_cai(where, width - 20.0f, &ui::theme::kTextDim);
    } else {
        ImGui::TextUnformatted(ICON_FA_PLANE "  No aircraft");
    }
    ImGui::EndGroup();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    const float sessionH = ImGui::GetTextLineHeightWithSpacing() * 2.0f + 14.0f;
    const float cardH    = ImGui::GetContentRegionAvail().y - sessionH - ImGui::GetStyle().ItemSpacing.y;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::theme::kPopupBg);
    ImGui::BeginChild("##sharecard", ImVec2(0.0f, cardH), ImGuiChildFlags_Borders);
    if (const auto& data = mSession.Data_cai())
        ui::ui_shareability_card_cai(*data, false);
    else
        ui::ui_wrapped_text_cai("Shareability: analyse the aircraft to see how well it can be shared.", &ui::theme::kTextDim);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::theme::kPanelBgDark);
    ImGui::BeginChild("##session", ImVec2(0.0f, sessionH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos(ImVec2(8.0f, 6.0f));
    ImGui::BeginGroup();
    const net::NetSession& net = mHub.Session_cai();
    ui::ui_text_ellipsis_cai(fmt::format(ICON_FA_CIRCLE " {}", net::StatusLine_cai(net)), width - 20.0f,
                             net.Active_cai() ? &ui::theme::kSavedText : nullptr);
    const std::string master = net.Active_cai() ? net.NameOf_cai(net::kHostId) : std::string("-");
    const std::string pic = net.Active_cai() ? net.NameOf_cai(net.PicId_cai()) : std::string("-");
    ImGui::TextDisabled("Master: %s   Pilot in command: %s   v%s.%s", master.c_str(), pic.c_str(), info::kVersion.data(),
                        info::kBuild.data());
    ImGui::EndGroup();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

// The About popup.
void HomePage::ui_popup_about_cai()
{
    ui::ui_popup_center_next_cai();
    if (!ImGui::BeginPopupModal(kPopupAbout, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        return;
    ImGui::TextUnformatted(info::kPluginName.data());
    ImGui::TextDisabled("Version %s.%s  -  network protocol %u", info::kVersion.data(), info::kBuild.data(),
                        static_cast<unsigned>(net::kProtocolVersion));
    ImGui::TextDisabled("Everybody in a session needs the same protocol.");
    ImGui::Spacing();
    ImGui::Separator();                             // the setup options, between two lines
    ImGui::Spacing();
    // The flight trace (for testing): saved in [debug], applies at once.
    bool trace = mSettings.debug.flightTrace;
    if (ImGui::Checkbox("Write a flight trace (for testing)", &trace)) {
        mSettings.debug.flightTrace = trace;
        std::string error;
        mSettings.Save_cai(error);
        LOG_INFO("flight trace: switched {} on the About box", trace ? "on" : "off");
    }
    ImGui::SameLine();                              // a (?) marker: clickable in VR (0.9.26)
    ui::ui_help_marker_cai("While a session runs, 10 rows a second of position, inputs, control deflections, brakes, "
                           "throttle, RPM and - with Follow - how far this aircraft is from the PIC's. Send the files of "
                           "every X-Plane to compare them. A new session starts a new file (the old one is kept as .1.csv).");
    {
        const net::FlightStats& flight = mHub.Flight_cai();
        const std::string state = !trace ? std::string("off")
                                  : flight.traceWriting ? fmt::format("writing ({:.1f} KB)", flight.traceBytes / 1024.0)
                                                        : std::string("on - writes while a session runs");
        ImGui::TextDisabled("Trace: %s", state.c_str());
        ui::ui_wrapped_text_cai(flight.tracePath.empty() ? std::string("shared_cockpit_trace.csv") : flight.tracePath, &ui::theme::kTextDim);
    }
    ImGui::Spacing();
    // The plugin log's detail (for testing): saved in [debug], applies at once, in any build.
    {
        const bool detailed = log::IsEnabled_cai(log::Level::Debug);
        ImGui::TextUnformatted("Plugin log:");
        ImGui::SameLine();
        int choice = detailed ? 1 : 0;
        bool picked = ImGui::RadioButton("Normal##loglevel", &choice, 0);
        ImGui::SameLine();
        picked |= ImGui::RadioButton("Detailed (debug)##loglevel", &choice, 1);
        if (picked && (choice == 1) != detailed) {
            mSettings.debug.logLevel = choice;
            log::SetLevel_cai(choice == 1 ? log::Level::Debug : log::Level::Info);
            std::string error;
            mSettings.Save_cai(error);
            LOG_INFO("log level: {} (About box)", choice == 1 ? "debug" : "info");
        }
        ImGui::SameLine();
        ui::ui_help_marker_cai("Detailed adds window, page and network details to the plugin log (larger log). "
                               "Saved; applies at once.");
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted(fmt::format("Designed by {}", info::kDesignedBy).c_str());
    ImGui::TextUnformatted(fmt::format("Coded by {}", info::kCodedBy).c_str());
    // ImGui::TextUnformatted(fmt::format("License: {}", info::kLicense).c_str());
    ImGui::Spacing();
    ui::ui_donate_button_cai("about", ImVec2(140.0f, 0.0f));     // 0.9.23
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Shared Cockpit is free. If you enjoy it, a donation helps.");
    ImGui::Spacing();
    // This computer: name and addresses, to tell the others (0.9.12).
    ui::ui_my_addresses_cai(mAddresses, mMachine);
    ImGui::Spacing();
    // The libraries we build with (versions from their headers).
    if (ImGui::CollapsingHeader("Libraries")) {
        std::vector<util::Library> libraries = {
            {"Dear ImGui", IMGUI_VERSION, "MIT", "the windows"},
            {"ImgWindow", "", "BSD-2-Clause", "ImGui in X-Plane windows"},
            {"X-Plane SDK", fmt::format("{}.{}.{}", kXPLM_Version / 100, kXPLM_Version / 10 % 10, kXPLM_Version % 10),
             "Laminar Research", "the X-Plane plugin API"},
        };
        for (auto& library : util::CoreLibraries_cai())
            libraries.push_back(std::move(library));
        if (ImGui::BeginTable("##libraries", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            for (const util::Library& library : libraries) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(library.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", library.version.empty() ? "-" : library.version.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s - %s", library.license.c_str(), library.use.c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::Spacing();
    if (ui::ui_button_cai("Close", ImVec2(ImGui::GetContentRegionAvail().x, 40.0f)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace sc::pages
