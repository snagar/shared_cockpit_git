// shared_cockpit - the Seats window (0.9.31)
#include "seats/SeatsWindow.h"

#include "IconsFontAwesome5.h"
#include "fmt/format.h"

#include "shared/log/Log.h"
#include "shared/sync/SeatSync.h"
#include "shared/ui/SeatRow.h"
#include "shared/ui/Theme.h"
#include "shared/ui/Widgets.h"

namespace sc {

// A small decorated window in the middle of the screen (X-Plane keeps where it is moved to).
SeatsWindow::SeatsWindow(net::SessionHub& hub, settings::Settings& settings)
    : ImgWindow(WindowGeometry::Centered(kWidth, kHeight, xplm_WindowDecorationRoundRectangle),
                xplm_WindowDecorationRoundRectangle, xplm_WindowLayerFloatingWindows),
      mHub(hub),
      mSettings(settings)
{
    SetWindowTitle("Shared Cockpit - Seats");
    SetWindowResizingLimits(300, kHeight, 700, kHeight + 60);
    ImGui::GetIO().IniFilename = nullptr;
    ui::theme::ApplyBaseStyle_cai();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(10.0f, 8.0f);
    LOG_INFO("seats window created");
}

// No scroll bars.
ImGuiWindowFlags_ SeatsWindow::beforeBegin()
{
    return static_cast<ImGuiWindowFlags_>(ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

// The seat row, where the seats come from, and the main window button.
void SeatsWindow::buildInterface()
{
    if (!mSeats) {
        ImGui::TextDisabled("Seats are not available (the plugin is disabled).");
        return;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(ICON_FA_CHAIR "  Seat");
    ImGui::SameLine(0.0f, 4.0f);
    ui::ui_help_marker_cai(ui::SeatRowHelp_cai());
    ImGui::SameLine();
    if (ui::ui_seat_row_cai(*mSeats, mSettings.alerts)) {
        std::string error;
        mSettings.Save_cai(error);
    }
    // Where the seats come from, and the main window button at the right end.
    const std::size_t count = mSeats->Seats_cai().size();
    const net::NetSession& session = mHub.Session_cai();
    const std::string from = count == 0 ? std::string("The vrconfig_helper plugin can add seats")
                           : fmt::format("{} seat{} from {}{}", count, count == 1 ? "" : "s", mSeats->Source_cai(),
                                         session.Active_cai() ? " - everybody sees where you sit" : "");
    const float icon = ImGui::GetFontSize() + 8.0f;
    ImGui::Spacing();
    ui::ui_text_ellipsis_cai(from, ImGui::GetContentRegionAvail().x - icon - 8.0f, &ui::theme::kTextDim);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - icon);
    if (ui::ui_icon_button_cai("##main", ICON_FA_WINDOW_MAXIMIZE))
        mMainPressed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show / hide the Shared Cockpit window");
}

// From the flight loop.
void SeatsWindow::Tick_cai()
{
    if (mMainPressed) {
        mMainPressed = false;
        if (mToggleMain)
            mToggleMain();
    }
}

} // namespace sc
