// shared_cockpit - reusable ImGui widgets and popups (taken from vrconfig_helper)
//
// Functions that draw UI start with "ui_", popups with "ui_popup_". They work on
// the current ImGui context, so any ImgWindow can use them.

#pragma once

#include <string>
#include <vector>

#include "imgui.h"

#include "shared/net/LocalAddresses.h"
#include "shared/ui/Theme.h"

namespace sc::ui {

// Disabled "(?)" marker; hovering it shows `desc`, wrapped inside the window.
void ui_help_marker_cai(const char* desc);
// A small square button that is just its icon, a row high (0.9.28, from the Flight deck): a background
// on hover / press, or always when `lit`; grey and not clickable when not `enabled`.
bool ui_icon_button_cai(const char* id, const char* icon, const ImVec4& color = theme::kButton, bool lit = false,
                        bool enabled = true);
// An on / off icon button with a tick / cross badge (0.9.29). True when switched (`on` is flipped).
bool ui_icon_toggle_cai(const char* id, const char* icon, bool& on);
// "— Title (?) ———": a SeparatorText with a help marker after the title (0.9.26).
void ui_separator_help_cai(const char* title, const char* help);

// Button with its own background colour (hover / pressed shades derived from it), white
// caption. Disabled = grey, not clickable. Returns true when pressed.
bool ui_color_button_cai(const char* label, const ImVec2& size, const ImVec4& color, bool enabled = true);

// The standard blue button, which can be disabled.
bool ui_button_cai(const char* label, const ImVec2& size = ImVec2(0, 0), bool enabled = true);
// The Donate button (0.9.23): a Ko-fi / PayPal choice (0.9.24) that opens the page in the browser;
// copies the link when it cannot.
// `small`: one text line high (the Connect page). `id` keeps two buttons on one page apart.
void ui_donate_button_cai(const char* id, const ImVec2& size = ImVec2(0, 0), bool small = false);

// Full-width coloured strip with wrapped text (status line). `height` 0 = fit the text.
void ui_status_strip_cai(const std::string& text, const ImVec4& bg, const ImVec4& textColor, float width = 0.0f,
                         float height = 0.0f);

// Text centred in the remaining width.
void ui_centered_text_cai(const char* text);

// Text wrapped at the window edge, optionally coloured.
void ui_wrapped_text_cai(const std::string& text, const ImVec4* color = nullptr);

// This computer's name and addresses: "Name: saar-pc, IP: 192.168.1.20 (LAN)", the values in aqua.
// Inside a thin purple box (0.9.15); a click on the name or an address copies it, "Copied" shows
// for 2 s (0.9.19). Optional: a help marker after the box, and a Refresh icon (true when pressed).
bool ui_my_addresses_cai(const std::vector<net::LocalAddress>& addresses, const std::string& machine,
                         const char* help = nullptr, bool refresh = false);
// The same line without the box.
void ui_address_line_cai(const std::vector<net::LocalAddress>& addresses, const std::string& machine);

// Pulsing for what needs an answer now (0.9.12): a soft fade, about once a second - not a
// hard blink. Off ([alerts] pulse=0): steady colours.
void ui_set_pulse_cai(bool on);
// 0..1, the same phase everywhere (0 while off).
float ui_pulse_cai();
// `color` brightened by the pulse (`strength`: how much at the top).
ImVec4 ui_pulse_color_cai(const ImVec4& color, float strength = 0.45f);

// One line of text no wider than `maxWidth`, shortened in the middle when needed.
void ui_text_ellipsis_cai(const std::string& text, float maxWidth, const ImVec4* color = nullptr);

// `text` shortened in the middle ("Cessna_172SP_G1...n_v2.acf") to fit `maxWidth`.
std::string EllipsisMiddle_cai(const std::string& text, float maxWidth);

// A numbered step: round badge (blue = now, green check = done, grey = not yet),
// the title, an optional help marker and a separator.
void ui_step_title_cai(int number, const char* title, bool done, bool current, const char* help = nullptr);

// Width that makes `count` items fill the rest of the line.
float ui_fill_width_cai(int count, float reserve = 0.0f);

// A small rounded label with a coloured background (category / grade chip).
void ui_chip_cai(const char* text, const ImVec4& bg);

// Width of ui_chip_cai(text).
float ChipWidth_cai(const char* text);

// Three-state checkbox: all ticked, none, or some (drawn with a dash, orange).
// `locked` draws it grey and not clickable. Returns true when clicked.
enum class TriState { None, Some, All };
bool ui_tristate_box_cai(const char* id, TriState state, bool locked);

// Positions and sizes the next popup inside the current window (call before BeginPopupModal).
void ui_popup_center_next_cai();

// A message with a single Close button. Open it with ImGui::OpenPopup(id), call every frame.
void ui_popup_message_cai(const char* id, const std::string& message, const char* closeLabel = "Close");

} // namespace sc::ui
