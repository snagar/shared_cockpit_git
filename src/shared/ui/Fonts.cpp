// shared_cockpit - fonts shared by all windows

#include "shared/ui/Fonts.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "XPLMUtilities.h"

#include "ImgWindow.h"
#include "ImgFontAtlas.h"
#include "shared/log/Log.h"

// Compressed Font Awesome 5 Solid TTF (fa_solid_900_compressed_data / _size).
#include "fa-solid-900.inc"

namespace sc::fonts {

ImFont* regular    = nullptr;
ImFont* fontawsome = nullptr;

namespace {

// Glyph range for the icons listed in IconsFontAwesome5.h (must outlive the atlas).
const ImWchar kFontAwesomeRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };

// Full path of a font in X-Plane's Resources/fonts folder.
std::string XPlaneFontPath_cai(const char* fileName)
{
    char sysPath[1024] = {};
    XPLMGetSystemPath(sysPath);                       // native path, ends with a separator
    return std::string(sysPath) + "Resources/fonts/" + fileName;
}

} // namespace

// Creates the shared font atlas: DejaVu Sans with the icons merged, and the icons alone.
void Initialize_cai()
{
    ImgWindow::sFontAtlas = std::make_shared<ImgFontAtlas>();

    const std::string dejaVu = XPlaneFontPath_cai("DejaVuSans.ttf");
    std::error_code ec;
    if (std::filesystem::exists(dejaVu, ec))
        regular = ImgWindow::sFontAtlas->AddFontFromFileTTF(dejaVu.c_str(), kFontSize);
    if (!regular) {
        LOG_WARN("{} not found, using the ImGui default font", dejaVu);
        regular = ImgWindow::sFontAtlas->AddFontDefault();
    }
    {
        ImFontConfig merge;
        merge.MergeMode            = true;
        merge.PixelSnapH           = true;
        merge.GlyphMinAdvanceX     = kFontSize;
        merge.FontDataOwnedByAtlas = false;
        if (!ImgWindow::sFontAtlas->AddFontFromMemoryCompressedTTF(
                fa_solid_900_compressed_data, static_cast<int>(fa_solid_900_compressed_size),
                kFontSize, &merge, kFontAwesomeRanges))
            LOG_WARN("could not merge the icons into the regular font");
    }
    ImFontConfig cfg;
    cfg.PixelSnapH           = true;
    cfg.GlyphMinAdvanceX     = kFontSize;
    cfg.FontDataOwnedByAtlas = false;
    std::snprintf(cfg.Name, sizeof(cfg.Name), "fontawsome");
    fontawsome = ImgWindow::sFontAtlas->AddFontFromMemoryCompressedTTF(
        fa_solid_900_compressed_data, static_cast<int>(fa_solid_900_compressed_size),
        kFontSize, &cfg, kFontAwesomeRanges);
    if (!fontawsome)
        LOG_ERROR("failed to load the Font Awesome icon font");
    LOG_INFO("fonts loaded: regular={}, fontawsome={}", regular ? "ok" : "missing", fontawsome ? "ok" : "missing");
}

// Releases the atlas (after all windows are gone).
void Shutdown_cai()
{
#if defined(IMGUI_VERSION_NUM) && (IMGUI_VERSION_NUM >= 19200)
    if (ImGui::GetCurrentContext() != nullptr)
        ImGui::GetIO().Fonts = nullptr;
#endif
    regular    = nullptr;
    fontawsome = nullptr;
    ImgWindow::sFontAtlas.reset();
    ImgWindow::Shutdown();                            // flush deferred texture deletions
}

} // namespace sc::fonts
