// shared_cockpit - X-Plane plugin entry points
//
// Registers the commands "shared_cockpit/toggle_main_window" and
// "shared_cockpit/toggle_flight_deck", with Plugins > Shared Cockpit menu items.
// A flight loop polls the network session every frame, so the session keeps
// running while the window is closed.
// The XPlugin* names are required by X-Plane, so they keep their names.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "XPLMDefs.h"
#include "XPLMDisplay.h"
#include "XPLMMenus.h"
#include "XPLMProcessing.h"
#include "XPLMPlugin.h"
#include "XPLMUtilities.h"

#include "imgui.h"
#include "ImgWindow.h"

#include "shared/ui/Fonts.h"
#include "Globals.h"
#include "shared/log/Log.h"
#include "SharedInfo.h"
#include "shared/analysis/Session.h"
#include "shared/net/SessionHub.h"
#include "shared/settings/Settings.h"
#include "PluginWindow.h"
#include "flightdeck/FlightDeckWindow.h"
#include "seats/SeatsWindow.h"
#include "shared/sync/CommandSync.h"
#include "shared/sync/DatarefSync.h"
#include "shared/sync/FlightSync.h"
#include "shared/sync/SeatSync.h"
#include "shared/sync/Chimes.h"
#include "shared/ui/Widgets.h"
#include "shared/sync/FlightTrace.h"
#include "shared/net/Protocol.h"
#include "shared/sync/PositionSync.h"
#include "shared/sync/LoadSync.h"
#include "shared/sync/WeatherSync.h"
#include "shared/util/Utils.h"
#include "shared/xp/XPlane.h"

namespace {

using sc::PluginWindow;

std::unique_ptr<PluginWindow> gWindow;
sc::analysis::Session         gSession;       // outlives the window: unsaved ticks survive closing it
sc::net::SessionHub           gHub;           // the network session: outlives the window too
sc::settings::Settings        gSettings;      // shared_cockpit.ini
std::unique_ptr<sc::FlightDeckWindow> gDeck;
std::unique_ptr<sc::SeatsWindow>      gSeatsWindow;   // the small Seats window (0.9.31)
std::unique_ptr<sc::sync::CommandSync> gCommandSync;   // shares the commands while a session runs
std::unique_ptr<sc::sync::DatarefSync> gDatarefSync;   // shares the datarefs while a session runs
std::unique_ptr<sc::sync::PositionSync> gPositionSync; // positions, the move to the PIC
std::unique_ptr<sc::sync::WeatherSync>  gWeatherSync;  // the host's weather and time
std::unique_ptr<sc::sync::LoadSync>     gLoadSync;     // the PIC's fuel and payload
std::unique_ptr<sc::sync::FlightSync>   gFlightSync;
std::unique_ptr<sc::sync::Chimes>       gChimes;       // chimes for questions, control, messages (0.9.12)
std::unique_ptr<sc::sync::SeatSync>     gSeatSync;     // seats: the aircraft's vrconfig hotspots, my view   // flying together: the PIC's inputs, pause
std::unique_ptr<sc::sync::FlightTrace>  gFlightTrace;  // [debug] flight_trace: a CSV of the flight
XPLMCommandRef                gCmdToggle = nullptr;
XPLMCommandRef                gCmdToggleDeck = nullptr;
XPLMCommandRef                gCmdBringWindow = nullptr;
XPLMCommandRef                gCmdToggleSeats = nullptr;   // shared_cockpit/toggle_seats_window (0.9.31)
bool                          gWantOpenSeats = false;      // Home's Seats button: done in the flight loop
int                           gReshowSeats = 0;            // VR: frames until the Seats window shows again   // shared_cockpit/vr/bring_window_to_me (0.9.27)
bool                          gWantOpenDeck = false;
int                           gReshowMain = 0;          // VR seat change: frames until the window shows again (0.9.26)   // set while a window draws, done in the flight loop
XPLMMenuID                    gMenu      = nullptr;

// Shows or hides the window (created on first use).
void ToggleWindow_cai()
{
    ImGuiContext* prevContext = ImGui::GetCurrentContext();
    if (!gWindow) {
        gWindow = std::make_unique<PluginWindow>(gSession, gHub, gSettings);
        auto lmbda_openDeck = []() { gWantOpenDeck = true; };      // done in the flight loop
        gWindow->SetOpenFlightDeck_cai(lmbda_openDeck);
        auto lmbda_openSeats = []() { gWantOpenSeats = true; };
        gWindow->SetOpenSeats_cai(lmbda_openSeats);
        gWindow->SetSeats_cai(gSeatSync.get());
        gWindow->SetVisible(true);         // moves itself into VR when VR is on
        gWindow->BringWindowToFront();
        LOG_DEBUG("window created (in VR: {})", gWindow->IsInVR());
    } else {
        const bool show = !gWindow->GetVisible();
        if (show)
            gWindow->ShowPage_cai(gWindow->CurrentPage_cai());   // refresh: the aircraft may have changed
        gWindow->SetVisible(show);
        if (show && !gWindow->IsPoppedOut())
            gWindow->BringWindowToFront();
        LOG_DEBUG("window {}", show ? "shown" : "hidden");
    }
    if (prevContext)
        ImGui::SetCurrentContext(prevContext);
}

// Shows the flight deck (2D only) and hides the main window. Never called while a window draws.
void OpenFlightDeck_cai()
{
    if (sc::Globals::get_cai().inVr) {
        LOG_INFO("flight deck: not in VR (the main window is used there)");
        return;
    }
    ImGuiContext* prevContext = ImGui::GetCurrentContext();
    if (!gDeck) {
        gDeck = std::make_unique<sc::FlightDeckWindow>(gHub, gSettings);
        gDeck->SetSeats_cai(gSeatSync.get());
        auto lmbda_toggleMain = []() {
            // The light: hide the main window, or show it on the Connect page.
            if (gWindow && gWindow->GetVisible()) {
                ToggleWindow_cai();
            } else {
                ToggleWindow_cai();
                if (gWindow)
                    gWindow->ShowPage_cai(sc::Page::Connect);
            }
        };
        gDeck->SetToggleMain_cai(lmbda_toggleMain);
    }
    gDeck->SetVisible(true);
    gDeck->BringWindowToFront();
    if (gWindow && gWindow->GetVisible())
        gWindow->SetVisible(false);
    LOG_INFO("flight deck shown, main window hidden");
    if (prevContext)
        ImGui::SetCurrentContext(prevContext);
}

// Shows the Seats window (created on first use). `hideMain`: Home's button hides the main window.
// Never called while a window draws.
void OpenSeats_cai(bool hideMain)
{
    ImGuiContext* prevContext = ImGui::GetCurrentContext();
    if (!gSeatsWindow) {
        gSeatsWindow = std::make_unique<sc::SeatsWindow>(gHub, gSettings);
        gSeatsWindow->SetSeats_cai(gSeatSync.get());
        gSeatsWindow->SetToggleMain_cai([]() { ToggleWindow_cai(); });
    }
    gSeatsWindow->SetVisible(true);                 // moves itself into VR when VR is on
    if (!gSeatsWindow->IsInVR())
        gSeatsWindow->BringWindowToFront();
    if (hideMain && gWindow && gWindow->GetVisible())
        gWindow->SetVisible(false);
    LOG_INFO("seats window shown{}", hideMain ? ", main window hidden" : "");
    if (prevContext)
        ImGui::SetCurrentContext(prevContext);
}

// The Seats command: shows or hides the Seats window.
void ToggleSeats_cai()
{
    if (gSeatsWindow && gSeatsWindow->GetVisible()) {
        ImGuiContext* prevContext = ImGui::GetCurrentContext();
        gSeatsWindow->SetVisible(false);
        if (prevContext)
            ImGui::SetCurrentContext(prevContext);
        LOG_INFO("seats window hidden");
    } else {
        OpenSeats_cai(false);
    }
}

// The deck command: shows or hides the deck.
void ToggleFlightDeck_cai()
{
    if (gDeck && gDeck->GetVisible()) {
        gDeck->Remember_cai();
        gDeck->SetVisible(false);
        LOG_INFO("flight deck hidden");
    } else {
        OpenFlightDeck_cai();
    }
}

// Every frame: the network session (handshake, people, control, timeouts), then the
// window actions asked for while a window was drawing.
float FlightLoop_cai(float, float, int, void*)
{
    sc::log::FlushMirror_cai();                        // the network thread's warnings, written here on X-Plane's thread
    gHub.Poll_cai();
    if (gCommandSync)
        gCommandSync->Tick_cai();
    if (gDatarefSync)
        gDatarefSync->Tick_cai();
    if (gPositionSync)
        gPositionSync->Tick_cai();
    if (gWeatherSync)
        gWeatherSync->Tick_cai();
    if (gLoadSync)
        gLoadSync->Tick_cai();
    if (gFlightSync)
        gFlightSync->Tick_cai();
    if (gSeatSync)
        gSeatSync->Tick_cai();
    if (gChimes)
        gChimes->Tick_cai();
    if (gFlightTrace)
        gFlightTrace->Tick_cai();                         // after the inputs were written this frame
    if (gReshowMain > 0 && --gReshowMain == 0 && gWindow) {
        // Shown again, X-Plane places it in front of me (VR seat change, 0.9.26).
        ImGuiContext* prevContext = ImGui::GetCurrentContext();
        gWindow->SetVisible(true);
        if (prevContext)
            ImGui::SetCurrentContext(prevContext);
        LOG_INFO("window: brought to the new seat (VR)");
    }
    if (gReshowSeats > 0 && --gReshowSeats == 0 && gSeatsWindow) {
        ImGuiContext* prevContext = ImGui::GetCurrentContext();
        gSeatsWindow->SetVisible(true);
        if (prevContext)
            ImGui::SetCurrentContext(prevContext);
        LOG_INFO("seats window: brought to the new seat (VR)");
    }
    if (gWantOpenDeck) {
        gWantOpenDeck = false;
        OpenFlightDeck_cai();
    }
    if (gWantOpenSeats) {
        gWantOpenSeats = false;
        OpenSeats_cai(true);
    }
    if (gDeck)
        gDeck->Tick_cai();
    if (gSeatsWindow)
        gSeatsWindow->Tick_cai();
    return -1.0f;     // again next frame
}

// Brings my windows in front of me: none open, the main window opens (like the toggle); an open one in
// VR closes and opens again on the next frame, where X-Plane places it in front of me (the main window
// and the Seats window, 0.9.31); open in 2D, it comes to the front.
void BringWindow_cai()
{
    const bool main = gWindow && gWindow->GetVisible();
    const bool seats = gSeatsWindow && gSeatsWindow->GetVisible();
    if (!main && !seats) {
        ToggleWindow_cai();
        return;
    }
    ImGuiContext* prevContext = ImGui::GetCurrentContext();
    auto lmbda_bring = [](ImgWindow& window, int& reshow) {
        if (window.IsInVR()) {
            window.SetVisible(false);
            reshow = 2;
        } else if (!window.IsPoppedOut()) {
            window.BringWindowToFront();
        }
    };
    if (main)
        lmbda_bring(*gWindow, gReshowMain);
    if (seats)
        lmbda_bring(*gSeatsWindow, gReshowSeats);
    if (prevContext)
        ImGui::SetCurrentContext(prevContext);
}

// The toggle command's handler.
int CommandHandler_cai(XPLMCommandRef cmd, XPLMCommandPhase phase, void*)
{
    if (phase == xplm_CommandBegin && cmd == gCmdToggle)
        ToggleWindow_cai();
    else if (phase == xplm_CommandBegin && cmd == gCmdToggleDeck)
        ToggleFlightDeck_cai();
    else if (phase == xplm_CommandBegin && cmd == gCmdBringWindow)
        BringWindow_cai();
    else if (phase == xplm_CommandBegin && cmd == gCmdToggleSeats)
        ToggleSeats_cai();
    return 0;   // consumed
}

// Keeps the window usable when X-Plane enters / leaves VR.
void FollowVr_cai(int msg)
{
    sc::Globals::get_cai().inVr = msg == XPLM_MSG_ENTERED_VR;
    // The deck is 2D only: entering VR hides it and brings the main window instead.
    if (msg == XPLM_MSG_ENTERED_VR && gDeck && gDeck->GetVisible()) {
        gDeck->Remember_cai();
        gDeck->SetVisible(false);
        LOG_INFO("entered VR: flight deck hidden, main window shown");
        if (!gWindow || !gWindow->GetVisible())
            ToggleWindow_cai();
    }
    // The Seats window goes into VR and back like the main window (0.9.31).
    if (gSeatsWindow && gSeatsWindow->GetVisible()) {
        if (msg == XPLM_MSG_ENTERED_VR && !gSeatsWindow->IsPoppedOut()) {
            gSeatsWindow->SetWindowPositioningMode(xplm_WindowVR);
        } else if (msg == XPLM_MSG_EXITING_VR && gSeatsWindow->IsInVR()) {
            const WindowGeometry centered = WindowGeometry::Centered(sc::SeatsWindow::kWidth, sc::SeatsWindow::kHeight);
            gSeatsWindow->SetWindowPositioningMode(xplm_WindowPositionFree);
            gSeatsWindow->SetWindowGeometry(centered.left, centered.top, centered.right, centered.bottom);
        }
    }
    if (!gWindow || !gWindow->GetVisible())
        return;
    if (msg == XPLM_MSG_ENTERED_VR) {
        if (!gWindow->IsPoppedOut())
            gWindow->SetWindowPositioningMode(xplm_WindowVR);
    } else if (msg == XPLM_MSG_EXITING_VR && gWindow->IsInVR()) {
        // The 2D geometry is lost while in VR: back to the middle of the screen.
        const WindowGeometry centered = WindowGeometry::Centered(sc::info::kWindowWidth, sc::info::kWindowHeight);
        gWindow->SetWindowPositioningMode(xplm_WindowPositionFree);
        gWindow->SetWindowGeometry(centered.left, centered.top, centered.right, centered.bottom);
    }
}

} // namespace

// --- X-Plane plugin interface (names required by X-Plane) ------------------------------

PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc)
{
    std::snprintf(outName, 256, "%s %s.%s", sc::info::kPluginName.data(), sc::info::kVersion.data(), sc::info::kBuild.data());
    std::snprintf(outSig, 256, "%s", sc::info::kPluginId.data());
    std::snprintf(outDesc, 256, "%s", sc::info::kPluginDesc.data());

    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    sc::Globals& globals   = sc::Globals::get_cai();
    globals.pluginFolder   = sc::xp::GetPluginFolder_cai();
    globals.xplaneFolder   = sc::xp::GetXPlaneFolder_cai();
    globals.aircraftFolder = globals.xplaneFolder / "Aircraft";
    globals.xplaneVersion  = sc::xp::GetXPlaneVersion_cai();

    sc::log::SetMirrorSink_cai(XPLMDebugString, true);    // X-Plane's Log.txt (main thread only): warnings, errors, the lines below
    sc::log::SetMirrorLevel_cai(sc::log::Level::Warning);
    sc::log::Init_cai(sc::info::kPluginId, globals.pluginFolder);
    LOG_INFO("{} version {}.{} starting in X-Plane {}, log file: {}", sc::info::kPluginName, sc::info::kVersion,
             sc::info::kBuild, globals.xplaneVersion, sc::Utils::Utf8_cai(sc::log::GetLogFilePath_cai()));
    sc::log::Mirror_cai(fmt::format("version {}.{} (protocol {}), its log: {}", sc::info::kVersion, sc::info::kBuild,
                                    static_cast<unsigned>(sc::net::kProtocolVersion), sc::Utils::Utf8_cai(sc::log::GetLogFilePath_cai())));

    // A stale build (old and new object files mixed) crashes at the first frame: refuse to start instead.
    const std::string layoutHere = fmt::format("hub={} session={} settings={}", sizeof(sc::net::SessionHub),
                                               sizeof(sc::net::NetSession), sizeof(sc::settings::Settings));
    const std::string layoutCore = sc::net::CoreLayout_cai();
    if (layoutHere != layoutCore) {
        LOG_ERROR("stale build: the plugin sees {}, the core library {} - delete the build folder and build again",
                  layoutHere, layoutCore);
        sc::log::FlushMirror_cai();
        sc::log::SetMirrorSink_cai(nullptr);
        sc::log::Shutdown_cai();
        return 0;                                           // X-Plane does not load the plugin
    }

    std::string settingsError;
    gSettings.Load_cai(globals.pluginFolder / "shared_cockpit.ini", settingsError);
    if (gSettings.debug.logLevel >= 0)                       // [debug] log_level: info or debug, in any build
        sc::log::SetLevel_cai(gSettings.debug.logLevel == 1 ? sc::log::Level::Debug : sc::log::Level::Info);
    LOG_INFO("log level: {}{}", sc::log::IsEnabled_cai(sc::log::Level::Debug) ? "debug" : "info",
             gSettings.debug.logLevel < 0 ? " (the build's default)" : "");
    gHub.Session_cai().SetOwnRules_cai(gSettings.rules);   // the session's rules when I host
    XPLMRegisterFlightLoopCallback(FlightLoop_cai, -1.0f, nullptr);

    gCmdToggle = XPLMCreateCommand(sc::info::kCmdToggle.data(), sc::info::kCmdToggleDesc.data());
    XPLMRegisterCommandHandler(gCmdToggle, CommandHandler_cai, 1, nullptr);

    const int slot = XPLMAppendMenuItem(XPLMFindPluginsMenu(), sc::info::kPluginName.data(), nullptr, 0);
    gMenu = XPLMCreateMenu(sc::info::kPluginName.data(), XPLMFindPluginsMenu(), slot, nullptr, nullptr);
    XPLMAppendMenuItemWithCommand(gMenu, sc::info::kMenuToggle.data(), gCmdToggle);
    gCmdToggleDeck = XPLMCreateCommand(sc::info::kCmdToggleDeck.data(), sc::info::kCmdToggleDeckDesc.data());
    XPLMRegisterCommandHandler(gCmdToggleDeck, CommandHandler_cai, 1, nullptr);
    XPLMAppendMenuItemWithCommand(gMenu, sc::info::kMenuToggleDeck.data(), gCmdToggleDeck);
    gCmdBringWindow = XPLMCreateCommand(sc::info::kCmdBringWindow.data(), sc::info::kCmdBringWindowDesc.data());
    XPLMRegisterCommandHandler(gCmdBringWindow, CommandHandler_cai, 1, nullptr);
    gCmdToggleSeats = XPLMCreateCommand(sc::info::kCmdToggleSeats.data(), sc::info::kCmdToggleSeatsDesc.data());
    XPLMRegisterCommandHandler(gCmdToggleSeats, CommandHandler_cai, 1, nullptr);
    XPLMAppendMenuItemWithCommand(gMenu, sc::info::kMenuToggleSeats.data(), gCmdToggleSeats);

    LOG_INFO("started");
    return 1;
}

PLUGIN_API void XPluginStop(void)
{
    XPLMUnregisterFlightLoopCallback(FlightLoop_cai, nullptr);
    gHub.Leave_cai("the plugin stopped");
    if (gCmdToggle) {
        XPLMUnregisterCommandHandler(gCmdToggle, CommandHandler_cai, 1, nullptr);
        gCmdToggle = nullptr;
    }
    if (gCmdToggleDeck) {
        XPLMUnregisterCommandHandler(gCmdToggleDeck, CommandHandler_cai, 1, nullptr);
        gCmdToggleDeck = nullptr;
    }
    if (gCmdBringWindow) {
        XPLMUnregisterCommandHandler(gCmdBringWindow, CommandHandler_cai, 1, nullptr);
        gCmdBringWindow = nullptr;
    }
    if (gCmdToggleSeats) {
        XPLMUnregisterCommandHandler(gCmdToggleSeats, CommandHandler_cai, 1, nullptr);
        gCmdToggleSeats = nullptr;
    }
    if (gMenu) {
        XPLMDestroyMenu(gMenu);
        gMenu = nullptr;
    }
    gSession.Clear_cai();
    LOG_INFO("stopped");
    sc::log::FlushMirror_cai();
    sc::log::SetMirrorSink_cai(nullptr);   // nothing reaches the SDK after XPluginStop (e.g. from static destructors)
    sc::log::Shutdown_cai();
}

PLUGIN_API int XPluginEnable(void)
{
    sc::fonts::Initialize_cai();
    gCommandSync = std::make_unique<sc::sync::CommandSync>(gSession, gHub);
    gDatarefSync = std::make_unique<sc::sync::DatarefSync>(gSession, gHub, gSettings);
    gPositionSync = std::make_unique<sc::sync::PositionSync>(gHub, gSettings);
    gWeatherSync = std::make_unique<sc::sync::WeatherSync>(gHub, gSettings);
    gLoadSync = std::make_unique<sc::sync::LoadSync>(gHub);
    gFlightSync = std::make_unique<sc::sync::FlightSync>(gHub);
    gSeatSync = std::make_unique<sc::sync::SeatSync>(gHub);
    // A VR seat change: hide the open window, show it the next frame: it comes in front of me.
    auto lmbda_arrived = []() {
        const bool mainVr = gWindow && gWindow->GetVisible() && gWindow->IsInVR();
        const bool seatsVr = gSeatsWindow && gSeatsWindow->GetVisible() && gSeatsWindow->IsInVR();
        if (gSettings.alerts.vrWindowFollows && (mainVr || seatsVr))
            BringWindow_cai();
    };
    gSeatSync->SetOnVrArrived_cai(lmbda_arrived);
    gChimes = std::make_unique<sc::sync::Chimes>(gHub, gSettings);
    sc::ui::ui_set_pulse_cai(gSettings.alerts.pulse);
    if (gDeck)
        gDeck->SetSeats_cai(gSeatSync.get());
    if (gWindow)
        gWindow->SetSeats_cai(gSeatSync.get());
    if (gSeatsWindow)
        gSeatsWindow->SetSeats_cai(gSeatSync.get());
    gFlightTrace = std::make_unique<sc::sync::FlightTrace>(gHub, sc::Globals::get_cai().pluginFolder, gSettings);
    LOG_INFO("enabled");
    return 1;
}

PLUGIN_API void XPluginDisable(void)
{
    // No drawing happens while disabled, so the window can be deleted directly.
    if (gCommandSync)
        gCommandSync->Stop_cai("the plugin was disabled");
    gCommandSync.reset();
    if (gDatarefSync)
        gDatarefSync->Stop_cai("the plugin was disabled");
    gDatarefSync.reset();
    gPositionSync.reset();
    gWeatherSync.reset();
    gLoadSync.reset();
    if (gFlightSync)
        gFlightSync->Stop_cai("the plugin was disabled");   // the user's own hardware back
    gFlightSync.reset();
    if (gDeck)
        gDeck->SetSeats_cai(nullptr);
    if (gWindow)
        gWindow->SetSeats_cai(nullptr);
    if (gSeatsWindow)
        gSeatsWindow->SetSeats_cai(nullptr);
    gSeatSync.reset();
    gChimes.reset();
    gFlightTrace.reset();
    gHub.Leave_cai("the plugin was disabled");
    if (gDeck)
        gDeck->Remember_cai();
    gDeck.reset();
    gSeatsWindow.reset();
    gWindow.reset();
    sc::fonts::Shutdown_cai();
    LOG_INFO("disabled");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int msg, void* param)
{
    switch (msg) {
        case XPLM_MSG_ENTERED_VR:
        case XPLM_MSG_EXITING_VR:
            LOG_DEBUG("{} VR", msg == XPLM_MSG_ENTERED_VR ? "entered" : "exiting");
            FollowVr_cai(msg);
            break;
        case XPLM_MSG_AIRPORT_LOADED:
            // A new flight (our move to the PIC sends this, not always PLANE_LOADED).
            if (gPositionSync)
                gPositionSync->OnPlaneLoaded_cai();
            break;
        case XPLM_MSG_PLANE_LOADED:
            // param is the plane index; 0 = the user's aircraft.
            if (reinterpret_cast<intptr_t>(param) == 0) {
                LOG_INFO("user aircraft loaded");
                if (gPositionSync && gPositionSync->OnPlaneLoaded_cai())
                    break;                              // our own move to the PIC: the session goes on
                if (gHub.Session_cai().State_cai() != sc::net::SessionState::Idle)
                    gHub.Leave_cai("the aircraft changed: left the session (everybody must fly the same aircraft)");
                if (gWindow)
                    gWindow->OnAircraftLoaded_cai();
            }
            break;
        default:
            break;
    }
}
