// shared_cockpit - the plugin's main window

#include "PluginWindow.h"
#include "shared/ui/Clipboard.h"

#include <string>

#include "shared/log/Log.h"
#include "SharedInfo.h"
#include "shared/ui/Theme.h"

namespace sc {

PluginWindow::PluginWindow(analysis::Session& session, net::SessionHub& hub, settings::Settings& settings)
    : ImgWindow(WindowGeometry::Centered(info::kWindowWidth, info::kWindowHeight, xplm_WindowDecorationRoundRectangle),
                xplm_WindowDecorationRoundRectangle, xplm_WindowLayerFloatingWindows),
      mSession(session),
      mHome(session, hub, settings),
      mAnalyse(session),
      mConnect(session, hub, settings),
      mSessionSettings(hub, settings)
{
    ui::InstallClipboard_cai();                     // Linux: Ctrl+C / Ctrl+V reach the system clipboard (0.9.23)
    SetWindowTitle(std::string(info::kPluginName));
    SetWindowResizingLimits(info::kWindowMinWidth, info::kWindowMinHeight, info::kWindowMaxWidth, info::kWindowMaxHeight);
    auto lmbda_goHome    = [this]() { ShowPage_cai(Page::Home); };
    auto lmbda_goAnalyse = [this]() { ShowPage_cai(Page::Analyse); };
    auto lmbda_goConnect = [this]() { ShowPage_cai(Page::Connect); };
    mHome.SetOpenAnalyse_cai(lmbda_goAnalyse);
    mHome.SetOpenConnect_cai(lmbda_goConnect);
    mAnalyse.SetGoHome_cai(lmbda_goHome);
    mConnect.SetGoHome_cai(lmbda_goHome);
    auto lmbda_goSettings = [this]() { ShowPage_cai(Page::SessionSettings); };
    mConnect.SetOpenSettings_cai(lmbda_goSettings);
    mSessionSettings.SetGoBack_cai(lmbda_goConnect);
    // The ImgWindow constructor leaves this window's ImGui context current.
    ImGui::GetIO().IniFilename = nullptr;          // no imgui.ini in X-Plane's folder
    ui::theme::ApplyBaseStyle_cai();
    mHome.Refresh_cai();
}

// No scroll bar on the window itself: the pages scroll their own lists.
ImGuiWindowFlags_ PluginWindow::beforeBegin()
{
    return static_cast<ImGuiWindowFlags_>(ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

// Draws the current page.
void PluginWindow::buildInterface()
{
    mSession.Poll_cai();
    switch (mPage) {
        case Page::Home:    mHome.ui_draw_cai();    break;
        case Page::Analyse: mAnalyse.ui_draw_cai(); break;
        case Page::Connect: mConnect.ui_draw_cai(); break;
        case Page::SessionSettings: mSessionSettings.ui_draw_cai(); break;
    }
}

// Shows `page` (refreshes it first).
void PluginWindow::ShowPage_cai(Page page)
{
    switch (page) {
        case Page::Home:    mHome.Refresh_cai();    break;
        case Page::Analyse: mAnalyse.Refresh_cai(); break;
        case Page::Connect: mHome.Refresh_cai(); mConnect.Refresh_cai(); break;   // Home: back to the loaded aircraft
        case Page::SessionSettings: break;
    }
    LOG_DEBUG("page {}", page == Page::Home ? "Home" : page == Page::Analyse ? "Analyse" : page == Page::Connect ? "Connect" : "Session settings");
    mPage = page;
}

// The user aircraft was (re)loaded.
void PluginWindow::OnAircraftLoaded_cai()
{
    mHome.Refresh_cai();
    mAnalyse.OnAircraftLoaded_cai();
}

} // namespace sc
