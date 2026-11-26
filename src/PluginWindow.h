// shared_cockpit - the plugin's main window
//
// One ImgWindow whose content changes with the page:
//   Home    - the steps of a shared flight (HomePage)
//   Analyse - pick an aircraft, read its files, choose what to share (AnalysePage)
//   Connect - host or join a session, the people, control (ConnectPage)
//   SessionSettings - the session's rules (SessionSettingsPage, from the Connect page's gear)
// Resizable between info::kWindowMin* and info::kWindowMax*.

#pragma once

#include "ImgWindow.h"

#include "shared/analysis/Session.h"
#include "analyse/AnalysePage.h"
#include "connect/ConnectPage.h"
#include "connect/SessionSettingsPage.h"
#include "home/HomePage.h"

namespace sc {

enum class Page { Home, Analyse, Connect, SessionSettings };

class PluginWindow : public ImgWindow {
public:
    PluginWindow(analysis::Session& session, net::SessionHub& hub, settings::Settings& settings);
    ~PluginWindow() override = default;

    PluginWindow(const PluginWindow&)            = delete;
    PluginWindow& operator=(const PluginWindow&) = delete;

    // Shows `page` (refreshes it first).
    void ShowPage_cai(Page page);
    // The page shown now.
    Page CurrentPage_cai() const { return mPage; }
    // What the Connect page's "Flight deck" button calls (empty = disabled).
    void SetOpenFlightDeck_cai(std::function<void()> open)
    {
        mHome.SetOpenFlightDeck_cai(open);
        mConnect.SetOpenFlightDeck_cai(std::move(open));
    }
    // The user aircraft was (re)loaded.
    void OnAircraftLoaded_cai();

protected:
    // ImgWindow: window flags (no scroll bar on the window itself).
    ImGuiWindowFlags_ beforeBegin() override;
    // ImgWindow: draws the current page.
    void buildInterface() override;

private:
    analysis::Session& mSession;
    Page               mPage = Page::Home;
    pages::HomePage    mHome;
    pages::AnalysePage mAnalyse;
    pages::ConnectPage mConnect;
    pages::SessionSettingsPage mSessionSettings;
};

} // namespace sc
