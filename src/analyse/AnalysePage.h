// shared_cockpit - the Analyse page: pick an aircraft, read it, choose what to share
//
//   1 Pick the airplane    loaded aircraft, or another .acf below X-Plane's Aircraft folder
//   2 Read the files       background scan with progress and Stop
//   3 Review and pick      filter box (by type or by system), foldable groups with
//                          three-state ticks, the list of controls, the shareability card
// Bottom: status, Save, Discard. A ticked control is shared with the other pilot.

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "analyse/AircraftFinder.h"
#include "shared/analysis/Analysis.h"
#include "shared/analysis/Session.h"

namespace sc::pages {

class AnalysePage {
public:
    explicit AnalysePage(analysis::Session& session);

    // Called by the Home button.
    void SetGoHome_cai(std::function<void()> goHome) { mGoHome = std::move(goHome); }
    // Called when the page is shown.
    void Refresh_cai();
    // The user aircraft was (re)loaded.
    void OnAircraftLoaded_cai();
    // Draws the page.
    void ui_draw_cai();
    // Groups the list by system path (true) or by manipulator type (false).
    void SetGroupBySystem_cai(bool bySystem) { mView = bySystem ? View::System : View::Type; }

private:
    enum class View { Type, System };
    // Which controls the list shows.
    enum class Show { All, Shared, NotShared, Changed };

    // One group of the list: a manipulator type, or a system path.
    struct ViewGroup {
        std::string                                    key;         // type, or system path
        std::string                                    row;         // filter-box row: category label, or first path part
        std::optional<analysis::Category>              category;    // empty = mixed (system view)
        std::vector<std::pair<std::size_t, std::size_t>> entries;   // (group index, item index) in the data
        std::size_t                                    shareable = 0;
        std::size_t                                    shared    = 0;
    };

    // Builds the groups of the current view when the data or the view changed.
    void RebuildView_cai();
    // Recounts shared / shareable of every group (after a tick).
    void Recount_cai();

    // The top bar: Home, aircraft name and path, help.
    void ui_top_bar_cai();
    // Steps 1 and 2 side by side.
    void ui_pick_and_read_cai();
    // Step 1: loaded or other aircraft.
    void ui_pick_cai();
    // Step 2: read the files.
    void ui_read_cai();
    // The filter box: one row per category (type view) or namespace (system view).
    void ui_filter_box_cai();
    // Group by, All, None, Fold all.
    void ui_toolbar_cai();
    // The list of groups and controls.
    void ui_list_cai();
    // The bottom row: status strip, Save, Discard, Next.
    void ui_bottom_row_cai();
    // The shareability card and its "What does this mean?" popup.
    void ui_share_card_cai(float height);
    // The popup that explains, per system, what can be shared.
    void ui_popup_explain_cai();
    // The full description of a control, for its tooltip.
    void ui_row_tooltip_cai(std::size_t group, std::size_t item);
    // True when the Show filter lets this control through.
    bool RowShown_cai(std::size_t group, std::size_t item) const;

    // Ticks every control of `group` (or none).
    void TickGroup_cai(const ViewGroup& group, bool share);
    // True when switching aircraft must wait (unsaved changes or a running scan).
    bool Busy_cai() const;

    analysis::Session&       mSession;
    std::function<void()>    mGoHome;
    View                     mView = View::Type;
    Show                     mShow = Show::All;
    std::vector<std::vector<std::string>> mDuplicateOf;   // per control: the type of an earlier control with the same refs
    bool                     mAskExplain = false;
    std::set<std::string>    mHidden[2];       // per view: hidden group keys
    std::set<std::string>    mFolded[2];       // per view: folded group keys
    std::vector<ViewGroup>   mGroups;
    std::size_t              mBuiltRevision = static_cast<std::size_t>(-1);
    View                     mBuiltView = View::Type;

    bool                     mOtherAircraft = false;
    analysis::AircraftFinder mFinder;
    std::vector<std::filesystem::path> mAcfList;
    bool                     mAcfListReady = false;
    std::string              mAcfFilter;
};

} // namespace sc::pages
