// shared_cockpit - fonts shared by all windows (taken from vrconfig_helper)
//
//   fonts::regular     DejaVu Sans from X-Plane (fallback: ImGui default font),
//                      with the Font Awesome icons merged in: ICON_FA_* work in any label
//   fonts::fontawsome  Font Awesome 5 Solid icons on their own

#pragma once

#include "imgui.h"
#include "IconsFontAwesome5.h"

namespace sc::fonts {

inline constexpr float kFontSize = 16.0f;

extern ImFont* regular;
extern ImFont* fontawsome;

// Creates the shared ImgWindow font atlas and loads the fonts. Call from
// XPluginEnable, before any window is created.
void Initialize_cai();
// Releases the atlas. Call from XPluginDisable, after all windows are gone.
void Shutdown_cai();

} // namespace sc::fonts
