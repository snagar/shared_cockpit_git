// shared_cockpit - reusable ImGui widgets and popups

#include "shared/ui/Widgets.h"

#include "shared/ui/Clipboard.h"
#include "shared/ui/Links.h"
#include "SharedInfo.h"

#include <cfloat>
#include <cmath>

#include <algorithm>

#include "IconsFontAwesome5.h"
#include "imgui_internal.h"                     // PushItemFlag: a (?) marker works in a disabled block

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
    const float wrap = std::max(ImGui::GetFontSize() * 10.0f,
                                std::min(ImGui::GetFontSize() * 35.0f,
                                         ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x * 4.0f));
    ImGui::PushID(desc);
    // Help stays readable in a read-only (disabled) block, e.g. a joiner's Session settings (0.9.26).
    const bool disabled = (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    if (disabled) {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, false);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
    }
    ImGui::TextDisabled("(?)");
    // Hover shows it; a click keeps it open (0.9.19: VR pointers do not hover well).
    if (ImGui::IsItemClicked())
        ImGui::OpenPopup("##help");
    if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("##help")) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kHelpText);
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(wrap);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
        ImGui::PopStyleColor();
    }
    if (ImGui::BeginPopup("##help")) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::kHelpText);
        ImGui::PushTextWrapPos(wrap);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }
    if (disabled) {
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
    }
    ImGui::PopID();
}

// A SeparatorText with a (?) after its title: the line starts after the marker.
void ui_separator_help_cai(const char* title, const char* help)
{
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
    ui_help_marker_cai(help);
    const float from = ImGui::GetItemRectMax().x + 6.0f;
    const float to = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (to > from)
        draw->AddLine(ImVec2(from, lineY), ImVec2(to, lineY), lineColor, style.SeparatorTextBorderSize);
}

// A small square icon button, vertically centred on the row.
bool ui_icon_button_cai(const char* id, const char* icon, const ImVec4& color, bool lit, bool enabled)
{
    const float side = ImGui::GetFontSize() + 8.0f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float rowH = ImGui::GetFrameHeight();
    if (rowH > side)
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + (rowH - side) * 0.5f));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(side, side));
    ImGui::EndDisabled();
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    ImVec4 bg = enabled ? color : theme::kButtonOff;
    bg.w = held ? 1.0f : lit ? 1.0f : 0.0f;         // filled only while pressed, or lit
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (bg.w > 0.0f)
        draw->AddRectFilled(pos, ImVec2(pos.x + side, pos.y + side), ImGui::GetColorU32(bg), 4.0f);
    if (hovered && !held)                           // pointed at: an outline only (0.9.29: never looks "on")
        draw->AddRect(pos, ImVec2(pos.x + side, pos.y + side), ImGui::GetColorU32(color), 4.0f, 0, 1.5f);
    const ImVec2 iconSize = ImGui::CalcTextSize(icon);
    const ImVec4& iconColor = !enabled ? theme::kTextDim : bg.w > 0.0f ? theme::kTextOnColor : theme::kText;
    draw->AddText(ImVec2(pos.x + (side - iconSize.x) * 0.5f, pos.y + (side - iconSize.y) * 0.5f),
                  ImGui::GetColorU32(iconColor), icon);
    return pressed;
}

// An on / off icon button: the state is a badge, not a colour (0.9.29). On: the icon with a green tick;
// off: the icon dimmed with a red cross. Pointed at: an outline. True when switched.
bool ui_icon_toggle_cai(const char* id, const char* icon, bool& on)
{
    const float side = ImGui::GetFontSize() + 8.0f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float rowH = ImGui::GetFrameHeight();
    if (rowH > side)
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + (rowH - side) * 0.5f));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(side, side));
    if (pressed)
        on = !on;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + side, pos.y + side);
    if (ImGui::IsItemActive())
        draw->AddRectFilled(pos, end, ImGui::GetColorU32(theme::kButtonOff), 4.0f);
    else if (ImGui::IsItemHovered())
        draw->AddRect(pos, end, ImGui::GetColorU32(theme::kButton), 4.0f, 0, 1.5f);
    const ImVec2 iconSize = ImGui::CalcTextSize(icon);
    draw->AddText(ImVec2(pos.x + (side - iconSize.x) * 0.5f - 2.0f, pos.y + (side - iconSize.y) * 0.5f - 1.0f),
                  ImGui::GetColorU32(on ? theme::kText : theme::kTextDim), icon);
    // The badge, lower right, on a round patch so it reads over the icon.
    const float badge = ImGui::GetFontSize() * 0.55f;
    const ImVec2 centre(end.x - badge * 0.55f, end.y - badge * 0.55f);
    draw->AddCircleFilled(centre, badge * 0.62f, ImGui::GetColorU32(ImGuiCol_WindowBg));
    const char* mark = on ? ICON_FA_CHECK : ICON_FA_TIMES;
    ImFont* font = ImGui::GetFont();
    const ImVec2 markSize = font->CalcTextSizeA(badge, FLT_MAX, 0.0f, mark);
    draw->AddText(font, badge, ImVec2(centre.x - markSize.x * 0.5f, centre.y - markSize.y * 0.5f),
                  ImGui::GetColorU32(on ? theme::kSavedText : theme::kErrorText), mark);
    return pressed;
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

namespace {
bool gPulse = true;

} // namespace

namespace {
// The "Copied" label: which value, until when.
std::string       gCopiedId;
std::string       gCopiedText;
double            gCopiedUntil = 0.0;
} // namespace

// This computer's name and addresses in a thin purple box; the values copy on a click.
bool ui_my_addresses_cai(const std::vector<net::LocalAddress>& addresses, const std::string& machine, const char* help, bool refresh)
{
    // Inside a thin purple box (rounded): stands out, and purple means no "do something" here.
    const ImVec2 pad(8.0f, 4.0f);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(start.x + pad.x, start.y + pad.y));
    ImGui::BeginGroup();
    ui_address_line_cai(addresses, machine);
    ImGui::EndGroup();
    const ImVec2 end = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRect(start, ImVec2(end.x + pad.x, end.y + pad.y), ImGui::GetColorU32(theme::kCatDataref), 4.0f, 0,
                                        1.5f);
    ImGui::SetCursorScreenPos(ImVec2(start.x, end.y + pad.y));
    ImGui::Dummy(ImVec2(end.x + pad.x - start.x, 2.0f));
    // After the box: Refresh and the help marker.
    bool refreshed = false;
    if (refresh) {
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, start.y));
        const float side = end.y + pad.y - start.y;      // as tall as the box: the whole icon shows
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        if (ui_button_cai(ICON_FA_REDO "##addrRefresh", ImVec2(side + 6.0f, side)))
            refreshed = true;
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Look up this computer's addresses again\n(after starting ZeroTier / Tailscale or changing networks)");
    }
    if (help) {
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, start.y + pad.y));
        ui_help_marker_cai(help);
    }
    if (refresh || help) {
        ImGui::SetCursorScreenPos(ImVec2(start.x, end.y + pad.y + 2.0f));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));             // an item there: the cursor move is a layout, not an overflow
    }
    return refreshed;
}

// The line inside the box: "Name: x, IP: y (LAN)"; a click on a value copies it.
void ui_address_line_cai(const std::vector<net::LocalAddress>& addresses, const std::string& machine)
{
    auto lmbda_value = [](const std::string& text, const std::string& tip) {
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(theme::kAqua, "%s", text.c_str());
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        if (ImGui::IsItemHovered()) {                 // looks like a link: underline, hand cursor
            ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), max, ImGui::GetColorU32(theme::kAqua), 1.0f);
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s", (tip + (tip.empty() ? "" : "\n") + "Click to copy").c_str());
        }
        if (ImGui::IsItemClicked()) {
            gCopiedId = text;
            gCopiedText = Copy_cai(text) ? std::string(ICON_FA_CHECK " Copied")
                                         : "Copy needs " + (CopyNeeds_cai().empty() ? std::string("a clipboard") : CopyNeeds_cai());
            gCopiedUntil = ImGui::GetTime() + 2.0;
        }
        // "Copied": 2 s, black on yellow, just above the value (placed by the item, not the mouse).
        if (gCopiedId == text && ImGui::GetTime() < gCopiedUntil) {
            const ImVec2 size = ImGui::CalcTextSize(gCopiedText.c_str());
            const ImVec2 at(min.x, min.y - size.y - 8.0f);
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            draw->AddRectFilled(at, ImVec2(at.x + size.x + 12.0f, at.y + size.y + 6.0f), IM_COL32(255, 228, 92, 255), 4.0f);
            draw->AddText(ImVec2(at.x + 6.0f, at.y + 3.0f), IM_COL32(20, 20, 20, 255), gCopiedText.c_str());
        }
    };
    ImGui::TextUnformatted("Name:");
    lmbda_value(machine.empty() ? std::string("-") : machine,
                "This computer's name. On most home networks the others can type it instead of\n"
                "the address (or name.local on macOS / Linux); with Tailscale, its MagicDNS name.");
    if (addresses.empty()) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextUnformatted(", IP:");
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(theme::kAqua, "none found");
        return;
    }
    for (std::size_t i = 0; i < addresses.size(); ++i) {
        const net::LocalAddress& a = addresses[i];
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextUnformatted(i == 0 ? ", IP:" : ",");
        lmbda_value(a.ip, a.adapter);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextDisabled("(%s)", a.kind.c_str());
    }
}

// Pulsing on or off.
void ui_set_pulse_cai(bool on)
{
    gPulse = on;
}

// 0..1, a soft fade about once a second.
float ui_pulse_cai()
{
    if (!gPulse)
        return 0.0f;
    const double t = ImGui::GetTime();
    return 0.5f - 0.5f * static_cast<float>(std::cos(t * 2.0 * 3.14159265358979 / 1.1));
}

// `color` brightened by the pulse.
ImVec4 ui_pulse_color_cai(const ImVec4& color, float strength)
{
    const float k = ui_pulse_cai() * strength;
    return ImVec4(color.x + (1.0f - color.x) * k, color.y + (1.0f - color.y) * k, color.z + (1.0f - color.z) * k, color.w);
}

// The standard blue button.
bool ui_button_cai(const char* label, const ImVec2& size, bool enabled)
{
    return ui_color_button_cai(label, size, theme::kButton, enabled);
}

// The Donate button: gold, a red heart; opens the Ko-fi / PayPal choice; a note for 3 s after.
void ui_donate_button_cai(const char* id, const ImVec2& size, bool small)
{
    static const ImVec4 kGold      = {0.988f, 0.792f, 0.243f, 1.0f};
    static const ImVec4 kGoldHover = {1.000f, 0.851f, 0.400f, 1.0f};
    static const ImVec4 kGoldDown  = {0.910f, 0.700f, 0.160f, 1.0f};
    static const ImVec4 kInk       = {0.090f, 0.150f, 0.330f, 1.0f};
    ImGui::PushID(id);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID noteId = ImGui::GetID("##donateNote");
    ImGui::PushStyleColor(ImGuiCol_Button, kGold);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kGoldHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kGoldDown);
    ImGui::PushStyleColor(ImGuiCol_Text, kInk);
    if (small)
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 1.0f));
    const bool clicked = ImGui::Button(ICON_FA_HEART "  Donate##donate", size);
    // The heart in red, over the button's ink one.
    {
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        const ImVec2 textSize = ImGui::CalcTextSize(ICON_FA_HEART "  Donate");
        const ImVec2 at(min.x + std::max(ImGui::GetStyle().FramePadding.x, (max.x - min.x - textSize.x) * 0.5f),
                        min.y + (max.y - min.y - textSize.y) * 0.5f);
        ImGui::GetWindowDrawList()->AddText(at, IM_COL32(214, 40, 57, 255), ICON_FA_HEART);
    }
    if (small)
        ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("##donateChoice"))
        ImGui::SetTooltip("Shared Cockpit is free. If you enjoy it, a donation helps.\n"
                          "Pick Ko-fi or PayPal; it opens in your web browser (on the desktop, outside VR).");
    if (clicked)
        ImGui::OpenPopup("##donateChoice");
    // The choice (0.9.24): Ko-fi or PayPal, under the button.
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##donateChoice")) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.839f, 0.157f, 0.224f, 1.0f), ICON_FA_HEART);
        ImGui::SameLine();
        ImGui::TextUnformatted("Thank you! Donate with:");
        const float side = ImGui::GetFrameHeight();
        ImGui::SameLine(std::max(ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + 16.0f,
                                 ImGui::GetWindowContentRegionMax().x - side));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
        if (ui_button_cai(ICON_FA_TIMES "##donateHide", ImVec2(side, side)))
            ImGui::CloseCurrentPopup();
        ImGui::PopStyleVar();
        ImGui::Separator();
        std::string url;
        if (ui_button_cai(ICON_FA_COFFEE "  Ko-fi", ImVec2(130.0f, 36.0f)))
            url = info::kDonateKofi;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", info::kDonateKofi.data());
        ImGui::SameLine();
        if (ui_button_cai(ICON_FA_CREDIT_CARD "  PayPal", ImVec2(130.0f, 36.0f)))
            url = info::kDonatePayPal;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("PayPal donation page (card or PayPal account)");
        if (!url.empty()) {
            const bool opened = OpenUrl_cai(url);
            const bool copied = !opened && Copy_cai(url);
            storage->SetFloat(noteId, static_cast<float>(ImGui::GetTime()));
            storage->SetInt(noteId + 1, opened ? 1 : copied ? 2 : 3);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    const float at = storage->GetFloat(noteId, -100.0f);
    if (ImGui::GetTime() - at < 3.0) {
        const int what = storage->GetInt(noteId + 1, 0);
        const char* note = what == 1 ? "Opened in your browser - thank you!"
                         : what == 2 ? "No browser here: the link is copied"
                                     : "Could not open the browser";
        ImGui::SetTooltip("%s", note);
    }
    ImGui::PopID();
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
