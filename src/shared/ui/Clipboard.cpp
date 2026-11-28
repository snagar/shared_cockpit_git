// shared_cockpit - copy text to the computer's clipboard, and paste from it (0.9.19, paste 0.9.23)
#include "shared/ui/Clipboard.h"

#include "imgui.h"

#include "shared/log/Log.h"

#if defined(__linux__)
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#endif

namespace sc::ui {

namespace {

#if defined(__linux__)
// The clipboard tool's command lines (empty when none): looked for once.
struct Tool {
    std::string copy;
    std::string paste;
};
const Tool& LinuxTools_cai()
{
    static const Tool tool = [] {
        auto lmbda_found = [](const char* name) {
            const char* path = std::getenv("PATH");
            if (!path)
                return false;
            std::string_view rest(path);
            while (!rest.empty()) {
                const std::size_t colon = rest.find(':');
                const std::string dir(rest.substr(0, colon));
                std::error_code ec;
                if (!dir.empty() && std::filesystem::exists(std::filesystem::path(dir) / name, ec))
                    return true;
                if (colon == std::string_view::npos)
                    break;
                rest.remove_prefix(colon + 1);
            }
            return false;
        };
        Tool found;
        // timeout: a clipboard owner that never answers must not hang X-Plane.
        if (std::getenv("WAYLAND_DISPLAY") && lmbda_found("wl-copy"))
            found = {"wl-copy", lmbda_found("wl-paste") ? "timeout 1 wl-paste --no-newline" : ""};
        else if (lmbda_found("xclip"))
            found = {"xclip -selection clipboard", "timeout 1 xclip -selection clipboard -o"};
        else if (lmbda_found("xsel"))
            found = {"xsel --clipboard --input", "timeout 1 xsel --clipboard --output"};
        LOG_INFO("clipboard: {}", found.copy.empty() ? std::string("no tool found (wl-copy, xclip or xsel)")
                                                     : found.copy + (found.paste.empty() ? ", no paste" : ", paste: " + found.paste));
        return found;
    }();
    return tool;
}

// ImGui's hooks: Ctrl+C / Ctrl+V in text fields.
const char* GetText_cai(ImGuiContext*)
{
    static std::string text;                        // ImGui copies it at once
    text = Paste_cai();
    return text.c_str();
}
void SetText_cai(ImGuiContext*, const char* text)
{
    Copy_cai(text ? std::string(text) : std::string());
}
#endif

} // namespace

// True when this computer can copy.
bool CanCopy_cai()
{
#if defined(__linux__)
    return !LinuxTools_cai().copy.empty();
#else
    return true;
#endif
}

// What to install when copying is not available.
std::string CopyNeeds_cai()
{
    return CanCopy_cai() ? std::string() : std::string("wl-copy or xclip");
}

// Copies `text`.
bool Copy_cai(const std::string& text)
{
#if defined(__linux__)
    const std::string& tool = LinuxTools_cai().copy;
    if (tool.empty())
        return false;
    FILE* pipe = popen(tool.c_str(), "w");          // the tool keeps serving the text after we close
    if (!pipe)
        return false;
    const bool written = std::fwrite(text.data(), 1, text.size(), pipe) == text.size();
    const int status = pclose(pipe);
    if (!written || status != 0)
        LOG_WARN("clipboard: '{}' failed (status {})", tool, status);
    return written && status == 0;
#else
    ImGui::SetClipboardText(text.c_str());
    return true;
#endif
}

// The clipboard's text.
std::string Paste_cai()
{
#if defined(__linux__)
    const std::string& tool = LinuxTools_cai().paste;
    if (tool.empty())
        return {};
    FILE* pipe = popen((tool + " 2>/dev/null").c_str(), "r");
    if (!pipe)
        return {};
    std::string text;
    char buffer[1024];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0 && text.size() < 64 * 1024)
        text.append(buffer, n);
    pclose(pipe);                                   // empty clipboard: an error status, no text
    return text;
#else
    const char* text = ImGui::GetClipboardText();
    return text ? std::string(text) : std::string();
#endif
}

// Linux: the current context's clipboard hooks to the system clipboard.
void InstallClipboard_cai()
{
#if defined(__linux__)
    if (!ImGui::GetCurrentContext() || !CanCopy_cai())
        return;
    ImGuiPlatformIO& io = ImGui::GetPlatformIO();
    io.Platform_GetClipboardTextFn = &GetText_cai;
    io.Platform_SetClipboardTextFn = &SetText_cai;
#endif
}

} // namespace sc::ui
