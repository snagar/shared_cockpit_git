// shared_cockpit - reusable ImGui widgets and popups

#include "shared/ui/Widgets.h"

#include <algorithm>

#include "IconsFontAwesome5.h"

namespace sc::ui {

namespace {

const ImVec2 kPopupButton(150.0f, 36.0f);

// `baseColor` made lighter (factor > 1) or darker (factor < 1).
ImVec4 Shade_cai(const ImVec4& baseColor, float factor)
{
    return ImVec4(std::min(baseColor.x * factor, 1.0f), std::min(baseColor.y * factor, 1.0f),
                  std::min(baseColor.z * factor, 1.0f), baseColor.w);
}

} // namespace

// Applies the plugin's light style and colours to the current ImGui context.
void theme::ApplyBaseStyle_cai()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsLight(&style);
    style.WindowPadding    = ImVec2(14.0f, 12.0f);
    style.ItemSpacing      = ImVec2(8.0f, 7.0f);
    style.FramePadding     = ImVec2(8.0f, 5.0f);
    style.FrameRounding    = 3.0f;
    style.PopupRounding    = 4.0f;
    style.ChildRounding    = 3.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize  = 1.0f;
    style.PopupBorderSize  = 2.0f;
    style.ScrollbarSize    = 12.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]          = kPanelBg;
    colors[ImGuiCol_ChildBg]           = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg]           = kPopupBg;
    colors[ImGuiCol_Text]              = kText;
    colors[ImGuiCol_TextDisabled]      = kTextDim;
    colors[ImGuiCol_Button]            = kButton;
    colors[ImGuiCol_ButtonHovered]     = kButtonHover;
    colors[ImGuiCol_ButtonActive]      = kButtonActive;
    colors[ImGuiCol_FrameBg]           = kInputBg;
    colors[ImGuiCol_FrameBgHovered]    = ImVec4(1, 1, 1, 1);
    colors[ImGuiCol_FrameBgActive]     = ImVec4(1, 1, 1, 1);
    colors[ImGuiCol_Header]            = kButtonHover;
    colors[ImGuiCol_HeaderHovered]     = kButtonHover;
    colors[ImGuiCol_HeaderActive]      = kButton;
    colors[ImGuiCol_CheckMark]         = kButton;
    colors[ImGuiCol_Separator]         = kPanelBgDark;
    colors[ImGuiCol_Border]            = kButtonOff;
    colors[ImGuiCol_ModalWindowDimBg]  = ImVec4(0, 0, 0, 0);   // ImgWindow draws the dimming itself
    colors[ImGuiCol_TitleBg]           = kButton;
    colors[ImGuiCol_TitleBgActive]     = kButton;
    colors[ImGuiCol_TitleBgCollapsed]  = kButton;
}

// Disabled "(?)" marker; hovering it shows `desc`.
void ui_help_marker_cai(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        const float wrap = std::max(ImGui::GetFontSize() * 10.0f,
                                    std::min(ImGui::GetFontSize() * 35.0f,
                                             ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 4.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kHelpText);
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(wrap);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
        ImGui::PopStyleColor();
    }
}

// Button with its own background colour; grey and not clickable when disabled.
bool ui_color_button_cai(const char* label, const ImVec2& size, const ImVec4& color, bool enabled)
{
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button,        enabled ? color : theme::kButtonOff);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, enabled ? Shade_cai(color, 1.12f) : theme::kButtonOff);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  enabled ? Shade_cai(color, 0.85f) : theme::kButtonOff);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextOnColor);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    return pressed && enabled;
}

// The standard blue button.
bool ui_button_cai(const char* label, const ImVec2& size, bool enabled)
{
    return ui_color_button_cai(label, size, theme::kButton, enabled);
}

// Coloured strip with wrapped text (status line).
void ui_status_strip_cai(const std::string& text, const ImVec4& bg, const ImVec4& textColor, float width, float height)
{
    const ImVec2 pos   = ImGui::GetCursorScreenPos();
    const float  w     = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    const float  padX  = ImGui::GetStyle().FramePadding.x;
    const float  wrap  = w - 2.0f * padX;
    const ImVec2 tsize = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrap);
    const float  h     = height > 0.0f ? height : tsize.y + 2.0f * (ImGui::GetStyle().FramePadding.y + 2.0f);
    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), ImGui::GetColorU32(bg),
                                              ImGui::GetStyle().FrameRounding);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + padX, pos.y + (h - tsize.y) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(w, h));
}

// Text centred in the remaining width.
void ui_centered_text_cai(const char* text)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    const float textW = ImGui::CalcTextSize(text).x;
    if (avail > textW)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - textW) * 0.5f);
    ImGui::TextUnformatted(text);
}

// `text` shortened in the middle to fit `maxWidth`.
std::string EllipsisMiddle_cai(const std::string& text, float maxWidth)
{
    std::string shown = text;
    if (ImGui::CalcTextSize(text.c_str()).x > maxWidth && text.size() > 4) {
        std::size_t keep = text.size();
        while (keep > 2) {
            --keep;
            const std::size_t head = (keep * 2) / 3;
            const std::size_t tail = keep - head;
            shown = text.substr(0, head) + "..." + text.substr(text.size() - tail);
            if (ImGui::CalcTextSize(shown.c_str()).x <= maxWidth)
                break;
        }
    }
    return shown;
}

// One line of text no wider than `maxWidth`.
void ui_text_ellipsis_cai(const std::string& text, float maxWidth, const ImVec4* color)
{
    const std::string shown = EllipsisMiddle_cai(text, maxWidth);
    if (color)
        ImGui::TextColored(*color, "%s", shown.c_str());
    else
        ImGui::TextUnformatted(shown.c_str());
}

// Text wrapped at the window edge, optionally coloured.
void ui_wrapped_text_cai(const std::string& text, const ImVec4* color)
{
    if (color)
        ImGui::PushStyleColor(ImGuiCol_Text, *color);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    if (color)
        ImGui::PopStyleColor();
}

// A numbered step title with its badge and a separator.
void ui_step_title_cai(int number, const char* title, bool done, bool current, const char* help)
{
    const float  radius = ImGui::GetTextLineHeight() * 0.62f;
    const ImVec2 at     = ImGui::GetCursorScreenPos();
    const ImVec2 center(at.x + radius, at.y + ImGui::GetTextLineHeight() * 0.5f);
    const ImVec4& badge = done ? theme::kSaved : current ? theme::kButton : theme::kTextDim;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddCircleFilled(center, radius, ImGui::ColorConvertFloat4ToU32(badge));
    const std::string mark = done ? std::string(ICON_FA_CHECK) : std::to_string(number);
    const ImVec2 markSize = ImGui::CalcTextSize(mark.c_str());
    draw->AddText(ImVec2(center.x - markSize.x * 0.5f, center.y - markSize.y * 0.5f),
                  ImGui::ColorConvertFloat4ToU32(theme::kTextOnColor), mark.c_str());
    ImGui::Dummy(ImVec2(radius * 2.0f, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
    ImGui::TextColored(current ? theme::kButtonActive : done ? theme::kText : theme::kTextDim, "%s", title);
    if (help) {
        ImGui::SameLine();
        ui_help_marker_cai(help);
    }
    ImGui::Separator();
}

// Width that makes `count` items fill the rest of the line.
float ui_fill_width_cai(int count, float reserve)
{
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    return (ImGui::GetContentRegionAvail().x - reserve - spacing * static_cast<float>(count - 1)) /
           static_cast<float>(count);
}

// Width of a chip with `text`.
float ChipWidth_cai(const char* text)
{
    return ImGui::CalcTextSize(text).x + 14.0f;
}

// A small rounded label with a coloured background.
void ui_chip_cai(const char* text, const ImVec4& bg)
{
    const ImVec2 size(ChipWidth_cai(text), ImGui::GetTextLineHeight() + 2.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::ColorConvertFloat4ToU32(bg), size.y * 0.5f);
    draw->AddText(ImVec2(pos.x + 7.0f, pos.y + 1.0f), ImGui::ColorConvertFloat4ToU32(theme::kTextOnColor), text);
    ImGui::Dummy(size);
}

// Three-state checkbox drawn by hand: blue tick = all, orange dash = some, white = none.
bool ui_tristate_box_cai(const char* id, TriState state, bool locked)
{
    const float  side = ImGui::GetFrameHeight() - 4.0f;
    const ImVec2 pos  = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(locked);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();

    const ImVec4 fill = locked ? theme::kPopupBg
                      : state == TriState::All  ? theme::kButton
                      : state == TriState::Some ? theme::kChanged
                                                : (hovered ? ImVec4(1, 1, 1, 1) : theme::kInputBg);
    const ImVec4 edge = locked ? theme::kButtonOff
                      : state == TriState::None ? theme::kTextDim
                                                : (state == TriState::All ? theme::kButtonActive : theme::kChangedText);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + side, pos.y + side);
    draw->AddRectFilled(pos, end, ImGui::ColorConvertFloat4ToU32(fill), 3.0f);
    draw->AddRect(pos, end, ImGui::ColorConvertFloat4ToU32(edge), 3.0f);
    const ImU32 white = ImGui::ColorConvertFloat4ToU32(theme::kTextOnColor);
    if (locked) {                                   // not available: a grey slash
        draw->AddLine(ImVec2(pos.x + side * 0.25f, pos.y + side * 0.75f), ImVec2(pos.x + side * 0.75f, pos.y + side * 0.25f),
                      ImGui::ColorConvertFloat4ToU32(theme::kButtonOff), 2.0f);
    } else if (state == TriState::All) {
        const float s = side;
        draw->AddLine(ImVec2(pos.x + s * 0.22f, pos.y + s * 0.52f), ImVec2(pos.x + s * 0.42f, pos.y + s * 0.72f), white, 2.2f);
        draw->AddLine(ImVec2(pos.x + s * 0.42f, pos.y + s * 0.72f), ImVec2(pos.x + s * 0.78f, pos.y + s * 0.28f), white, 2.2f);
    } else if (state == TriState::Some) {
        draw->AddLine(ImVec2(pos.x + side * 0.25f, pos.y + side * 0.5f), ImVec2(pos.x + side * 0.75f, pos.y + side * 0.5f),
                      white, 2.4f);
    }
    return pressed && !locked;
}

// Positions and sizes the next popup inside the current window.
void ui_popup_center_next_cai()
{
    const ImVec2 center(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() * 0.5f,
                        ImGui::GetWindowPos().y + ImGui::GetWindowHeight() * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetWindowWidth() * 0.6f, 0.0f),
                                        ImVec2(ImGui::GetWindowWidth() - 30.0f, ImGui::GetWindowHeight() - 30.0f));
}

// A message with a single Close button.
void ui_popup_message_cai(const char* id, const std::string& message, const char* closeLabel)
{
    ui_popup_center_next_cai();
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ui_wrapped_text_cai(message);
        ImGui::Spacing();
        if (ui_button_cai(closeLabel, kPopupButton))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace sc::ui
