// shared_cockpit - copy text to the computer's clipboard, and paste from it (0.9.19, paste 0.9.23)
//
// Windows and macOS: Dear ImGui's own clipboard code (macOS: IMGUI_ENABLE_OSX_DEFAULT_CLIPBOARD_FUNCTIONS,
// CMakeLists). Linux: Dear ImGui has none, so the text goes to the first clipboard tool found:
// wl-copy (Wayland), else xclip or xsel (X11). Without one, copying is not available here.
// Paste (0.9.23): Ctrl+V in our text fields; Linux reads with wl-paste, xclip -o or xsel -o.

#pragma once

#include <string>

namespace sc::ui {

// True when this computer can copy (Linux: a clipboard tool was found).
bool CanCopy_cai();
// Copies `text`; false when it could not.
bool Copy_cai(const std::string& text);
// What to install when copying is not available ("wl-copy or xclip"), empty when it is.
std::string CopyNeeds_cai();
// The clipboard's text ("" when empty or not available).
std::string Paste_cai();
// Linux: points the current ImGui context's Ctrl+C / Ctrl+V at the system clipboard
// (call once per window, right after its context is made). Elsewhere ImGui does it already.
void InstallClipboard_cai();

} // namespace sc::ui
