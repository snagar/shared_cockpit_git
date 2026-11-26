// shared_cockpit - the flight deck: a small 2D window for flying in a session

#include "flightdeck/FlightDeckWindow.h"

#include <algorithm>
#include <chrono>

#include "fmt/format.h"
#include "IconsFontAwesome5.h"
#include "XPLMDisplay.h"

#include "shared/log/Log.h"
#include "shared/ui/Theme.h"
#include "shared/ui/Widgets.h"
#include "shared/util/Utils.h"

namespace sc {

namespace {

constexpr float kRowButtonH = 26.0f;
constexpr float kAnswerW    = 30.0f;

// The light's colour for a session state.
ImU32 LightColor_cai(net::SessionState state)
{
    switch (state) {
        case net::SessionState::Hosting:
        case net::SessionState::InSession:   return ImGui::GetColorU32(ui::theme::kSaved);
        case net::SessionState::Connecting:
        case net::SessionState::Handshaking: return ImGui::GetColorU32(ui::theme::kChanged);
        case net::SessionState::Failed:
        case net::SessionState::Lost:        return ImGui::GetColorU32(ui::theme::kError);
        case net::SessionState::Idle:        break;
    }
    return ImGui::GetColorU32(ui::theme::kButtonOff);
}

// "Ann's session · 3 people", "Hosting · 2 people", "Ended: removed by the master", ...
std::string Title_cai(const net::NetSession& session, bool withPic)
{
    const std::size_t people = session.People_cai().size();
    const std::string pic = session.PicId_cai() == session.MyId_cai() ? std::string("you") : session.NameOf_cai(session.PicId_cai());
    switch (session.State_cai()) {
        case net::SessionState::Idle:        return "Not connected";
        case net::SessionState::Connecting:  return "Connecting...";
        case net::SessionState::Handshaking: return "Joining...";
        case net::SessionState::Failed:      return fmt::format("Could not connect: {}", session.FailText_cai());
        case net::SessionState::Lost:        return fmt::format("Ended: {}", session.FailText_cai());
        case net::SessionState::Hosting:
            return withPic ? fmt::format("Hosting " "\xC2\xB7" " PIC {}", pic) : fmt::format("Hosting " "\xC2\xB7" " {} people", people);
        case net::SessionState::InSession:
            return withPic ? fmt::format("{}'s session " "\xC2\xB7" " PIC {}", session.NameOf_cai(net::kHostId), pic)
                           : fmt::format("{}'s session " "\xC2\xB7" " {} people", session.NameOf_cai(net::kHostId), people);
    }
    return {};
}

// A small square button that is just its icon (centred), with a hover / press background.
bool ui_icon_button_cai(const char* id, const char* icon, const ImVec4& color = ui::theme::kButton)
{
    const float side = ImGui::GetFontSize() + 8.0f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    // Vertically centred on the row's text.
    const float rowH = ImGui::GetFrameHeight();
    if (rowH > side)
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + (rowH - side) * 0.5f));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImVec4 bg = color;
    bg.w = held ? 1.0f : hovered ? 0.85f : 0.0f;               // only a background when pointed at
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (bg.w > 0.0f)
        draw->AddRectFilled(pos, ImVec2(pos.x + side, pos.y + side), ImGui::GetColorU32(bg), 4.0f);
    const ImVec2 iconSize = ImGui::CalcTextSize(icon);
    const ImVec4& iconColor = bg.w > 0.0f ? ui::theme::kTextOnColor : ui::theme::kText;
    draw->AddText(ImVec2(pos.x + (side - iconSize.x) * 0.5f, pos.y + (side - iconSize.y) * 0.5f),
                  ImGui::GetColorU32(iconColor), icon);
    return pressed;
}

// The X-Plane window's area in global boxels (left, top, right, bottom).
// XPLMGetScreenBoundsGlobal alone does not follow the X-Plane window when it is restored /
// resized in windowed mode (it can keep the desktop's bounds), so the size comes from
// XPLMGetScreenSize (the main window), placed at the global bounds' bottom-left corner.
void ScreenRect_cai(int& sl, int& st, int& sr, int& sb)
{
    int gl = 0, gt = 0, gr = 0, gb = 0;
    XPLMGetScreenBoundsGlobal(&gl, &gt, &gr, &gb);
    int width = 0, height = 0;
    XPLMGetScreenSize(&width, &height);
    sl = gl;
    sb = gb;
    sr = width > 0 ? gl + width : gr;
    st = height > 0 ? gb + height : gt;
    static int sLast[6] = {0, 0, 0, 0, 0, 0};
    const int now[6] = {gl, gt, gr, gb, width, height};
    if (!std::equal(std::begin(now), std::end(now), std::begin(sLast))) {
        LOG_DEBUG("screen: global bounds {},{},{},{}, main window {}x{} -> deck area {},{},{},{}", gl, gt, gr, gb,
                  width, height, sl, st, sr, sb);
        std::copy(std::begin(now), std::end(now), std::begin(sLast));
    }
}

// Moves a `width` x `height` window at left / top inside the screen (all monitors' bounds).
void KeepOnScreen_cai(int& left, int& top, int width, int height)
{
    int sl = 0, st = 0, sr = 0, sb = 0;
    ScreenRect_cai(sl, st, sr, sb);
    if (sr <= sl || st <= sb)
        return;
    left = std::clamp(left, sl, std::max(sl, sr - width));
    top = std::clamp(top, std::min(st, sb + height), st);
}

// Tooltips are drawn inside the deck's own window: in the small sizes they would be cut off,
// so they are shown only in Small (the light's colour and the title carry the state anyway).
bool gTooltips = true;

// SetTooltip, when the deck is large enough to show it.
void Tooltip_cai(const char* text)
{
    if (gTooltips)
        ImGui::SetTooltip("%s", text);
}

} // namespace

FlightDeckWindow::FlightDeckWindow(net::SessionHub& hub, settings::Settings& settings)
    : ImgWindow(WindowGeometry::FromTopLeft(0, 0, FlightDeckPanel::WidthOf_cai(Size::Small), FlightDeckPanel::HeightOf_cai(Size::Small)),
                xplm_WindowDecorationSelfDecorated, xplm_WindowLayerFloatingWindows),
      mHub(hub),
      mSettings(settings),
      mPanel(hub)
{
    SetWindowTitle("Shared Cockpit - flight deck");
    mChosen = static_cast<Size>(std::clamp(mSettings.flightDeck.size, 0, 2));
    mApplied = mChosen;
    // Where it was last time, or the top-right corner of the main screen.
    int left = mSettings.flightDeck.left, top = mSettings.flightDeck.top;
    if (left < 0 || top < 0) {
        int sl = 0, st = 0, sr = 0, sb = 0;
        ScreenRect_cai(sl, st, sr, sb);
        left = sr - FlightDeckPanel::WidthOf_cai(Size::Small) - 30;
        top = st - 70;
    }
    KeepOnScreen_cai(left, top, FlightDeckPanel::WidthOf_cai(mChosen), FlightDeckPanel::HeightOf_cai(mChosen));
    SetWindowGeometry(left, top, left + FlightDeckPanel::WidthOf_cai(mChosen), top - FlightDeckPanel::HeightOf_cai(mChosen));
    SetWindowDragArea();                              // the whole deck drags; its buttons still work
    ImGui::GetIO().IniFilename = nullptr;
    FlightDeckPanel::ApplyStyle_cai();
    LOG_INFO("flight deck created at {},{} size {}", left, top, static_cast<int>(mChosen));
}

// No scroll bars, no title.
ImGuiWindowFlags_ FlightDeckWindow::beforeBegin()
{
    return static_cast<ImGuiWindowFlags_>(ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                          ImGuiWindowFlags_NoTitleBar);
}

// The compact style: the main window's look with small paddings.
void FlightDeckPanel::ApplyStyle_cai()
{
    ui::theme::ApplyBaseStyle_cai();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(8.0f, 5.0f);       // the main window has 14 x 12
    style.ItemSpacing   = ImVec2(6.0f, 5.0f);
    style.WindowBorderSize = 0.0f;
}

// Width of a size.
int FlightDeckPanel::WidthOf_cai(Size size)
{
    switch (size) {
        case Size::Minimal: return 136;
        case Size::Line:    return 310;
        case Size::Small:   return 340;
    }
    return 340;
}

// Height of a size.
int FlightDeckPanel::HeightOf_cai(Size size)
{
    switch (size) {
        case Size::Minimal: return 36;   // one row: 26 + 2 x 5
        case Size::Line:    return 67;   // two rows
        case Size::Small:   return 132;
    }
    return 132;
}

// Small while a question is open, else the chosen size.
FlightDeckWindow::Size FlightDeckWindow::Shown_cai() const
{
    return mHub.Session_cai().Questions_cai().empty() && !mHub.Position_cai().ask ? mChosen : Size::Small;
}

// Resizes, keeping the top-left corner - or, near the screen's edge, moving the deck so it
// stays fully on screen (a deck in the top-right corner grows to the left and down).
void FlightDeckWindow::Apply_cai(Size size)
{
    int left = 0, top = 0, right = 0, bottom = 0;
    GetWindowGeometry(left, top, right, bottom);
    const int width = FlightDeckPanel::WidthOf_cai(size), height = FlightDeckPanel::HeightOf_cai(size);
    // Anchored on the right edge when the deck was there (it grows to the left).
    int sl = 0, st = 0, sr = 0, sb = 0;
    ScreenRect_cai(sl, st, sr, sb);
    if (sr > sl && right >= sr - 40)
        left = right - width;
    KeepOnScreen_cai(left, top, width, height);
    SetWindowGeometry(left, top, left + width, top - height);
    LOG_DEBUG("flight deck size {} -> {}", static_cast<int>(mApplied), static_cast<int>(size));
    mApplied = size;
}

// From the flight loop: the deferred actions and the size.
void FlightDeckWindow::Tick_cai()
{
    if (mPanel.TakeSizePressed_cai()) {
        // From the size on screen (a question may show Small): what the icon promised.
        mChosen = static_cast<Size>((static_cast<int>(mApplied) + 1) % 3);
        Remember_cai();
    }
    if (mPanel.TakeClosePressed_cai()) {
        Remember_cai();
        SetVisible(false);
        LOG_INFO("flight deck closed (the session goes on)");
    }
    if (mPanel.TakeLightPressed_cai() && mToggleMain)
        mToggleMain();
    FollowScreen_cai();
    if (GetVisible() && Shown_cai() != mApplied)
        Apply_cai(Shown_cai());
}

// When the X-Plane window (the screen bounds) changes, the deck snaps to the side edge it was
// nearer to and keeps its place from top to bottom as a ratio (kept inside the new bounds).
void FlightDeckWindow::FollowScreen_cai()
{
    int sl = 0, st = 0, sr = 0, sb = 0;
    ScreenRect_cai(sl, st, sr, sb);
    const int now[4] = {sl, st, sr, sb};
    const bool first = mScreen[0] == 0 && mScreen[1] == 0 && mScreen[2] == 0 && mScreen[3] == 0;
    const bool changed = !std::equal(std::begin(now), std::end(now), std::begin(mScreen));
    if (first || !changed || !GetVisible() || sr <= sl) {
        std::copy(std::begin(now), std::end(now), std::begin(mScreen));
        // Where the deck is while the screen stays the same. After a resize X-Plane may
        // already have moved the deck on its own, so the position before it is used.
        GetWindowGeometry(mLastDeck[0], mLastDeck[1], mLastDeck[2], mLastDeck[3]);
        return;
    }
    int left = mLastDeck[0], top = mLastDeck[1], right = mLastDeck[2], bottom = mLastDeck[3];
    if (right <= left || top <= bottom)
        GetWindowGeometry(left, top, right, bottom);
    const int width = right - left, height = top - bottom;
    constexpr int kMargin = 10;
    // Nearer edge in the old bounds.
    const bool nearRight = (mScreen[2] - right) < (left - mScreen[0]);
    left = nearRight ? sr - width - kMargin : sl + kMargin;
    // The same place from top to bottom, as a ratio of the room above / below the deck, so a
    // deck at the top stays at the top and one in the middle stays in the middle.
    const int oldRoom = (mScreen[1] - mScreen[3]) - height;
    const int newRoom = (st - sb) - height;
    const double ratio = oldRoom > 0 ? std::clamp(double(mScreen[1] - top) / oldRoom, 0.0, 1.0) : 0.0;
    top = st - static_cast<int>(ratio * std::max(0, newRoom) + 0.5);
    KeepOnScreen_cai(left, top, width, height);
    SetWindowGeometry(left, top, left + width, top - height);
    LOG_INFO("X-Plane window resized (bounds {},{},{},{} -> {},{},{},{}): flight deck to the {} edge, {:.0f}% down, at {},{}",
             mScreen[0], mScreen[1], mScreen[2], mScreen[3], sl, st, sr, sb, nearRight ? "right" : "left", ratio * 100.0,
             left, top);
    std::copy(std::begin(now), std::end(now), std::begin(mScreen));
    const int moved[4] = {left, top, left + width, top - height};
    std::copy(std::begin(moved), std::end(moved), std::begin(mLastDeck));
    Remember_cai();
}

// Remembers the position and the chosen size.
void FlightDeckWindow::Remember_cai()
{
    int left = 0, top = 0, right = 0, bottom = 0;
    GetWindowGeometry(left, top, right, bottom);
    mSettings.flightDeck.left = left;
    mSettings.flightDeck.top = top;
    mSettings.flightDeck.size = static_cast<int>(mChosen);
    std::string error;
    mSettings.Save_cai(error);
}

// Draws the panel in the window's size. The background is see-through (opacity from the
// settings) and solid while the mouse is over the deck or a question waits. Begin() has
// already used the colour, so a change shows from the next frame.
void FlightDeckWindow::buildInterface()
{
    const bool solid = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
                       !mHub.Session_cai().Questions_cai().empty() || mHub.Position_cai().ask;
    const float alpha = solid ? 1.0f : std::clamp(mSettings.flightDeck.opacity, 20, 100) / 100.0f;
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = alpha;
    mPanel.ui_draw_cai(mApplied);
}

// Draws the deck.
void FlightDeckPanel::ui_draw_cai(Size size)
{
    const net::NetSession& session = mHub.Session_cai();
    gTooltips = size == Size::Small;
    const float radius = ImGui::GetFontSize() * 0.42f;

    // Row 1: light, text, corner buttons.
    if (ui_light_cai(radius))
        mLightPressed = true;
    ImGui::SameLine();
    const float icon = ImGui::GetFontSize() + 8.0f;
    const int corner = (size == Size::Minimal ? 1 : 2) + (ShowMove_cai() ? 1 : 0);
    const float cornerW = icon * static_cast<float>(corner) + ImGui::GetStyle().ItemSpacing.x * static_cast<float>(corner - 1);
    const float textW = ImGui::GetContentRegionAvail().x - cornerW - ImGui::GetStyle().ItemSpacing.x;
    ImGui::AlignTextToFramePadding();
    if (size == Size::Minimal) {
        const net::Person* me = session.Find_cai(session.MyId_cai());
        const std::string text = !session.Active_cai() ? std::string("-")
                                 : session.IsHost_cai() ? fmt::format("{} ppl", session.People_cai().size())
                                                         : fmt::format("{} ms", me ? me->pingMs : 0);
        ui::ui_text_ellipsis_cai(text, textW);
    } else {
        std::string title = Title_cai(session, size == Size::Line);
        if (size == Size::Small && mHub.Sync_cai().running && mHub.Sync_cai().perMinute > 0)
            title += fmt::format("  " ICON_FA_EXCHANGE_ALT " {}/min", mHub.Sync_cai().perMinute);
        ui::ui_text_ellipsis_cai(title, textW);
    }
    if (ImGui::IsItemHovered())
        Tooltip_cai(net::StatusLine_cai(session).c_str());
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - cornerW);    // the buttons at the right edge
    ui_corner_buttons_cai(size);

    if (size == Size::Minimal)
        return;
    if (size == Size::Line) {
        if (!ui_question_cai())
            ui_last_event_cai();
        return;
    }

    // Small.
    if (session.Active_cai()) {
        const net::Person* me = session.Find_cai(session.MyId_cai());
        const bool iAmPic = session.PicId_cai() == session.MyId_cai();
        // Follow: a short state after the PIC's name; the whole line in the tooltip.
        const net::FlightStats& flight = mHub.Flight_cai();
        const net::StatusText follow = net::FollowLine_cai(session, flight);
        std::string state;
        if (!follow.text.empty() && !iAmPic)
            state = flight.farAway ? "  too far" : flight.holding ? "  holding" : flight.following ? fmt::format("  {:.1f} m", flight.gapM) : "  ...";
        ImGui::TextUnformatted(fmt::format(ICON_FA_PLANE "  PIC: {}", iAmPic ? std::string("you")
                                                                          : session.NameOf_cai(session.PicId_cai())).c_str());
        if (ImGui::IsItemHovered())
            Tooltip_cai(follow.text.empty() ? "PIC = pilot in command: the one who has the controls"
                                            : fmt::format("PIC = pilot in command: the one who has the controls\n{}", follow.text).c_str());
        if (!state.empty()) {
            ImGui::SameLine(0.0f, 0.0f);
            const ImVec4 color = follow.mark == net::StatusText::Mark::Warn ? ui::theme::kChangedText : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            ImGui::TextColored(color, "%s", state.c_str());
            if (ImGui::IsItemHovered())
                Tooltip_cai(follow.text.c_str());
        }
        ImGui::SameLine(ImGui::GetContentRegionMax().x * 0.55f);
        ImGui::TextDisabled("You: %s%s", me ? net::RoleName_cai(me->role) : "-", session.IsHost_cai() ? " (master)" : "");
        ui_control_buttons_cai();
    } else {
        ui::ui_wrapped_text_cai(session.State_cai() == net::SessionState::Idle
                                    ? "Press the light to open Shared Cockpit and host or join."
                                    : "Press the light to open the Connect page.",
                                &ui::theme::kTextDim);
    }
    ImGui::Separator();
    if (!ui_question_cai())
        ui_last_event_cai();
}

// The status light: a round button.
bool FlightDeckPanel::ui_light_cai(float radius)
{
    const ImVec2 size(radius * 2.0f + 8.0f, ImGui::GetFrameHeight());
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##light", size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 centre(at.x + size.x * 0.5f, at.y + size.y * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddCircleFilled(centre, radius + (hovered ? 1.5f : 0.0f), LightColor_cai(mHub.Session_cai().State_cai()));
    draw->AddCircle(centre, radius + (hovered ? 1.5f : 0.0f), ImGui::GetColorU32(ui::theme::kText), 0, 1.0f);
    if (hovered)
        Tooltip_cai(fmt::format("{}\nPress: show / hide the Shared Cockpit window", net::StatusLine_cai(mHub.Session_cai())).c_str());
    return pressed;
}

// Size button (cycles Minimal -> Line -> Small) and close button.
void FlightDeckPanel::ui_corner_buttons_cai(Size size)
{
    if (ShowMove_cai()) {
        // Only while my aircraft is off the PIC's position.
        if (ui_icon_button_cai("##move", ICON_FA_MAP_MARKER_ALT, ui::theme::kChanged))
            mHub.Position_cai().moveRequested = true;
        if (ImGui::IsItemHovered())
            Tooltip_cai(fmt::format("Move to the PIC's position\n{} from {}", Utils::DistanceText_cai(mHub.Position_cai().distanceM),
                                    mHub.Position_cai().picName).c_str());
        ImGui::SameLine();
    }
    // The icon says what a press does: expand while the deck can grow, compress back to Minimal from Small.
    const bool shrinks = size == Size::Small;
    if (ui_icon_button_cai("##size", shrinks ? ICON_FA_COMPRESS_ALT : ICON_FA_EXPAND_ALT))
        mSizePressed = true;
    if (ImGui::IsItemHovered())
        Tooltip_cai("Smaller: minimal");
    if (size == Size::Minimal)
        return;
    ImGui::SameLine();
    if (ui_icon_button_cai("##close", ICON_FA_TIMES, ui::theme::kError))
        mClosePressed = true;
    if (ImGui::IsItemHovered())
        Tooltip_cai("Close the deck (the session goes on).\nPlugins > Shared Cockpit opens it again.");
}

// The first open question with Yes / No.
bool FlightDeckPanel::ui_question_cai()
{
    net::NetSession& session = mHub.Session_cai();
    if (session.Questions_cai().empty())
        return ui_move_question_cai();
    const net::Question q = session.Questions_cai().front();
    const long long left = std::max<long long>(
        0, std::chrono::duration_cast<std::chrono::seconds>(q.until - std::chrono::steady_clock::now()).count());
    const float buttonsW = 2.0f * kAnswerW + ImGui::GetStyle().ItemSpacing.x;
    ImGui::AlignTextToFramePadding();
    ui::ui_text_ellipsis_cai(fmt::format(ICON_FA_HAND_PAPER " {} {} {}s", q.fromName,
                                         q.kind == net::Question::Kind::Request ? "asks for control" : "offers control", left),
                             ImGui::GetContentRegionAvail().x - buttonsW - ImGui::GetStyle().ItemSpacing.x,
                             &ui::theme::kChangedText);
    ImGui::SameLine();
    std::string why;
    if (ui::ui_color_button_cai(ICON_FA_CHECK "##yes", ImVec2(kAnswerW, 0.0f), ui::theme::kSaved))
        session.AnswerControl_cai(q.fromId, true, why);
    ImGui::SameLine();
    if (ui::ui_color_button_cai(ICON_FA_TIMES "##no", ImVec2(kAnswerW, 0.0f), ui::theme::kError))
        session.AnswerControl_cai(q.fromId, false, why);
    if (!why.empty())
        mHub.Note_cai(net::SessionEvent::Kind::Declined, why);
    return true;
}

// The question after joining: move to the PIC's position?
bool FlightDeckPanel::ui_move_question_cai()
{
    net::PositionStats& pos = mHub.Position_cai();
    if (!pos.ask || !mHub.Session_cai().Active_cai())
        return false;
    const float buttonsW = 2.0f * kAnswerW + ImGui::GetStyle().ItemSpacing.x;
    ImGui::AlignTextToFramePadding();
    ui::ui_text_ellipsis_cai(fmt::format(ICON_FA_MAP_MARKER_ALT " {} from {}: move?", Utils::DistanceText_cai(pos.distanceM), pos.picName),
                             ImGui::GetContentRegionAvail().x - buttonsW - ImGui::GetStyle().ItemSpacing.x, &ui::theme::kChangedText);
    if (ImGui::IsItemHovered())
        Tooltip_cai("Move your aircraft to the position of the PIC (pilot in command)\n"
                    "(near: a direct move; far or other engine state: a new flight)");
    ImGui::SameLine();
    if (ui::ui_color_button_cai(ICON_FA_CHECK "##moveyes", ImVec2(kAnswerW, 0.0f), ui::theme::kSaved))
        pos.moveRequested = true;
    ImGui::SameLine();
    if (ui::ui_color_button_cai(ICON_FA_TIMES "##moveno", ImVec2(kAnswerW, 0.0f), ui::theme::kError))
        pos.ask = false;
    return true;
}

// True while "Move to the PIC" applies: in a session, off, no move running.
bool FlightDeckPanel::ShowMove_cai() const
{
    const net::PositionStats& pos = mHub.Position_cai();
    return mHub.Session_cai().Active_cai() && pos.off && !pos.iAmPic && pos.move == net::PositionStats::Move::None;
}

// The last event.
void FlightDeckPanel::ui_last_event_cai()
{
    const auto& feed = mHub.Feed_cai();
    if (feed.empty()) {
        ImGui::TextDisabled("No events yet");
        return;
    }
    const net::FeedLine& line = feed.back();
    ui::ui_text_ellipsis_cai(fmt::format("{}  {}", line.time.substr(0, 5), line.text), ImGui::GetContentRegionAvail().x,
                             &ui::theme::kTextDim);
    if (ImGui::IsItemHovered())
        Tooltip_cai(line.text.c_str());
}

// Ask for control / Take control.
void FlightDeckPanel::ui_control_buttons_cai()
{
    net::NetSession& session = mHub.Session_cai();
    const net::Person* me = session.Find_cai(session.MyId_cai());
    const bool iAmPic = session.PicId_cai() == session.MyId_cai();
    const bool crew = me && me->role == net::Role::Crew;
    std::string why;
    const float w = session.IsHost_cai() ? (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.6f : -1.0f;
    if (ui::ui_button_cai(ICON_FA_HAND_PAPER "  Ask for control##deck", ImVec2(w, kRowButtonH), !iAmPic && crew))
        session.RequestControl_cai(why);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        Tooltip_cai(iAmPic ? "You are the pilot in command" : !crew ? "Observers cannot take control"
                                                                               : "The pilot in command answers yes / no");
    if (session.IsHost_cai()) {
        ImGui::SameLine();
        if (ui::ui_color_button_cai(ICON_FA_CROWN "  Take##deck", ImVec2(-1.0f, kRowButtonH), ui::theme::kChanged, !iAmPic))
            session.TakeControl_cai(why);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            Tooltip_cai("The master takes control at once, without asking.");
    }
    if (!why.empty())
        mHub.Note_cai(net::SessionEvent::Kind::Declined, why);
}

} // namespace sc
