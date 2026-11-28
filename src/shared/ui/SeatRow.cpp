// shared_cockpit - the seat row (0.9.30)
#include "shared/ui/SeatRow.h"

#include <algorithm>
#include <cfloat>

#include "IconsFontAwesome5.h"
#include "imgui.h"

#include "shared/sync/SeatSync.h"
#include "shared/ui/Widgets.h"

namespace sc::ui {

namespace {

constexpr float kListRowH = 24.0f;              // a seat in the open list: easy to hit in VR

} // namespace

// The help of the row's (?).
const char* SeatRowHelp_cai()
{
    return "Your seat is only your view: it changes nothing for the others. The back button takes you back to it after "
           "looking around (also the commands shared_cockpit/seat/back_to_my_seat, /next and /previous).\n\n"
           "In VR the view slides there: only the position, where you look stays with your headset. A seat "
           "that faces sideways gets a notice. Reset VR view stops a slide.\n\n"
           "The window button (tick: on, cross: off) is \"Window follows me (VR)\": after you pick a seat in VR, this window "
           "comes in front of you there (Back to my seat leaves it). The command "
           "shared_cockpit/vr/bring_window_to_me does it at any time.";
}

// The seat list, back, window follows me.
bool ui_seat_row_cai(sync::SeatSync& seats, settings::AlertSettings& alerts, bool enabled)
{
    const std::vector<net::SeatInfo>& list = seats.Seats_cai();
    if (list.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("No seats in this aircraft");
        ImGui::SameLine();
        ui_help_marker_cai("Seats come from the aircraft's vrconfig file (in a session: the host's aircraft). "
                           "The vrconfig_helper plugin can add them.");
        return false;
    }
    const std::uint8_t mine = seats.MySeat_cai();
    const float icon = ImGui::GetFontSize() + 8.0f;             // ui_icon_button_cai's side
    const float gap = 4.0f;
    ImGui::BeginDisabled(!enabled);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (icon + gap) * 2.0f);
    // The open list may be wider than the field: full names, never cut.
    float listW = ImGui::CalcTextSize("No seat (my own view)").x;
    for (const net::SeatInfo& seat : list)
        listW = std::max(listW, ImGui::CalcTextSize(seat.name.c_str()).x);
    listW += ImGui::GetStyle().WindowPadding.x * 2.0f + 8.0f;
    ImGui::SetNextWindowSizeConstraints(ImVec2(listW, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginCombo("##seatList", mine < list.size() ? list[mine].name.c_str() : "Pick a seat")) {
        if (ImGui::Selectable("No seat (my own view)", mine == net::kNoSeat))
            seats.RequestPick_cai(net::kNoSeat);
        for (std::size_t i = 0; i < list.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(list[i].name.c_str(), i == mine, ImGuiSelectableFlags_None, ImVec2(0.0f, kListRowH)))
                seats.RequestPick_cai(static_cast<std::uint8_t>(i));
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0.0f, gap);
    if (ui_icon_button_cai("##seatBack", seats.Sliding_cai() ? ICON_FA_ELLIPSIS_H : ICON_FA_UNDO, theme::kButton, false,
                           enabled && mine < list.size()))
        seats.RequestBack_cai();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Back to my seat");
    ImGui::SameLine(0.0f, gap);
    bool changed = false;
    if (enabled) {
        changed = ui_icon_toggle_cai("##follow", ICON_FA_WINDOW_RESTORE, alerts.vrWindowFollows);
    } else {
        bool shown = alerts.vrWindowFollows;            // read-only: drawn, a click does nothing
        ui_icon_toggle_cai("##follow", ICON_FA_WINDOW_RESTORE, shown);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(alerts.vrWindowFollows ? "Window follows me (VR): on" : "Window follows me (VR): off");
    return changed;
}

} // namespace sc::ui
