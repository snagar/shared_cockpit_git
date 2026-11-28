// shared_cockpit - open a web page in the computer's browser (0.9.23)
//
// Windows: ShellExecute. macOS: open. Linux: xdg-open. The browser opens on the desktop
// (in VR: take the headset off, or use the desktop view).

#pragma once

#include <string>

namespace sc::ui {

// Opens `url` (http / https only) in the default browser; false when it could not be started.
bool OpenUrl_cai(const std::string& url);

} // namespace sc::ui
