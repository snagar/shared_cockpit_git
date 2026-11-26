// shared_cockpit - reusable ImGui widgets and popups (taken from vrconfig_helper)
//
// Functions that draw UI start with "ui_", popups with "ui_popup_". They work on
// the current ImGui context, so any ImgWindow can use them.

#pragma once

#include <string>

#include "imgui.h"
#include "shared/ui/Theme.h"

namespace sc::ui {

// Disabled "(?)" marker; hovering it shows `desc`, wrapped inside the window.
void ui_help_marker_cai(const char* desc);

// Button with its own background colour (hover / pressed shades derived from it), white
// caption. Disabled = grey, not clickable. Returns true when pressed.
bool ui_color_button_cai(const char* label, const ImVec2& size, const ImVec4& color, bool enabled = true);

// The standard blue button, which can be disabled.
bool ui_button_cai(const char* label, const ImVec2& size = ImVec2(0, 0), bool enabled = true);

// Full-width coloured strip with wrapped text (status line). `height` 0 = fit the text.
void ui_status_strip_cai(const std::string& text, const ImVec4& bg, const ImVec4& textColor, float width = 0.0f,
                         float height = 0.0f);

// Text centred in the remaining width.
void ui_centered_text_cai(const char* text);

// Text wrapped at the window edge, optionally coloured.
void ui_wrapped_text_cai(const std::string& text, const ImVec4* color = nullptr);

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
