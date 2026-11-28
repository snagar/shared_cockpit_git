// shared_cockpit - colours and their roles (palette from vrconfig_helper)
//
//   orange = unsaved change / next thing to do, green = saved or done, red = error.
//   Category colours: commands blue, datarefs purple, can't sync red, not used grey.
//   Grade colours: full green, mostly blue, partial orange, none red, check grey.
// State colours are always shown together with a symbol or text.

#pragma once

#include "imgui.h"

namespace sc::ui::theme {

// Panel and text
inline const ImVec4 kPanelBg       = {0.725f, 0.737f, 0.800f, 1.0f};   // window background (grey-blue)
inline const ImVec4 kPanelBgDark   = {0.650f, 0.663f, 0.733f, 1.0f};   // headers, separators, neutral strips
inline const ImVec4 kPopupBg       = {0.850f, 0.860f, 0.910f, 1.0f};   // popups, group headers
inline const ImVec4 kInputBg       = {0.960f, 0.960f, 0.980f, 1.0f};   // inputs, list background
inline const ImVec4 kListRowLine   = {0.880f, 0.886f, 0.918f, 1.0f};
inline const ImVec4 kText          = {0.000f, 0.000f, 0.000f, 1.0f};
inline const ImVec4 kTextDim       = {0.330f, 0.340f, 0.400f, 1.0f};   // secondary / disabled text
inline const ImVec4 kTextOnColor   = {1.000f, 1.000f, 1.000f, 1.0f};
inline const ImVec4 kHelpText      = {0.050f, 0.050f, 0.100f, 1.0f};

// Buttons
inline const ImVec4 kButton        = {0.247f, 0.498f, 0.851f, 1.0f};
inline const ImVec4 kButtonHover   = {0.337f, 0.596f, 0.937f, 1.0f};
inline const ImVec4 kButtonActive  = {0.180f, 0.400f, 0.741f, 1.0f};
inline const ImVec4 kButtonOff     = {0.560f, 0.580f, 0.650f, 1.0f};

// States
inline const ImVec4 kChanged       = {0.851f, 0.451f, 0.051f, 1.0f};   // unsaved / next: backgrounds
inline const ImVec4 kChangedText   = {0.702f, 0.361f, 0.000f, 1.0f};
inline const ImVec4 kSaved         = {0.180f, 0.620f, 0.267f, 1.0f};   // saved / done: backgrounds
inline const ImVec4 kSavedText     = {0.118f, 0.478f, 0.200f, 1.0f};
inline const ImVec4 kError         = {0.780f, 0.180f, 0.180f, 1.0f};
inline const ImVec4 kErrorText     = {0.650f, 0.080f, 0.080f, 1.0f};
inline const ImVec4 kWarnYellow    = {0.960f, 0.720f, 0.040f, 1.0f};   // the warning triangle (0.9.20)
inline const ImVec4 kAqua          = {0.000f, 0.420f, 0.460f, 1.0f};   // my address / machine name (0.9.12): text, dark enough to read

// Categories
inline const ImVec4 kCatCommand    = kButton;
inline const ImVec4 kCatDataref    = {0.361f, 0.247f, 0.600f, 1.0f};
inline const ImVec4 kCatCantSync   = kErrorText;
inline const ImVec4 kCatNotUsed    = {0.435f, 0.451f, 0.522f, 1.0f};
inline const ImVec4 kCatMixed      = kTextDim;

// Grades
inline const ImVec4 kGradeFull     = kSavedText;
inline const ImVec4 kGradeMostly   = kButtonActive;
inline const ImVec4 kGradePartial  = kChangedText;
inline const ImVec4 kGradeNone     = kErrorText;
inline const ImVec4 kGradeCheck    = kTextDim;

// Applies the base style to the current ImGui context (once per window).
void ApplyBaseStyle_cai();

} // namespace sc::ui::theme
