// shared_cockpit - the Analyse page: pick an aircraft, read it, choose what to share

#include "analyse/AnalysePage.h"

#include <algorithm>
#include <map>

#include "fmt/format.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include "Globals.h"
#include "shared/analysis/VrSeats.h"
#include "shared/log/Log.h"
#include "shared/ui/ShareCard.h"
#include "shared/ui/Widgets.h"
#include "shared/util/Utils.h"
#include "shared/xp/XPlane.h"

namespace sc::pages {

namespace fs = std::filesystem;
using analysis::Category;

namespace {

constexpr float kShareCardWidth = 300.0f;
constexpr const char* kPopupExplain = "What can be shared";
constexpr float kRowLabelWidth  = 142.0f;    // the filter box's row buttons
constexpr float kBottomHeight   = 40.0f;
constexpr int   kFilterRows     = 3;         // rows visible in the filter box before it scrolls
constexpr float kPillPadY       = 1.0f;      // compact pills: less vertical padding

// The colour of a category (empty = mixed).
const ImVec4& CategoryColor_cai(std::optional<Category> category)
{
    if (!category)
        return ui::theme::kCatMixed;
    switch (*category) {
        case Category::Command:  return ui::theme::kCatCommand;
        case Category::Dataref:  return ui::theme::kCatDataref;
        case Category::CantSync: return ui::theme::kCatCantSync;
        case Category::NotUsed:  return ui::theme::kCatNotUsed;
    }
    return ui::theme::kCatMixed;
}

// The colour of a filter-box row in the system view, by its first path part.
const ImVec4& NamespaceColor_cai(const std::string& name)
{
    if (name == "sim")       return ui::theme::kCatCommand;
    if (name == "laminar")   return ui::theme::kButtonActive;
    if (name == "(screens)") return ui::theme::kCatCantSync;
    return ui::theme::kCatDataref;
}

// A tri-state from counts.
ui::TriState StateOf_cai(std::size_t shared, std::size_t shareable)
{
    if (shareable == 0 || shared == 0)
        return ui::TriState::None;
    return shared == shareable ? ui::TriState::All : ui::TriState::Some;
}

// A path below X-Plane's Aircraft folder ("Laminar Research/Cessna 172 SP/Cessna_172SP.acf").
std::string ShownAcfPath_cai(const fs::path& acf)
{
    std::error_code ec;
    const fs::path relative = fs::relative(acf, Globals::get_cai().aircraftFolder, ec);
    return ec || relative.empty() ? Utils::Utf8_cai(acf) : Utils::Utf8_cai(relative.generic_u8string());
}

} // namespace

AnalysePage::AnalysePage(analysis::Session& session)
    : mSession(session)
{
}

// True when switching aircraft must wait.
bool AnalysePage::Busy_cai() const
{
    return mSession.Dirty_cai() || mSession.Scanning_cai();
}

// Called when the page is shown: follows the loaded aircraft unless another one is chosen.
void AnalysePage::Refresh_cai()
{
    if (mOtherAircraft || Busy_cai())
        return;
    if (const auto loaded = xp::GetUserAircraft_cai()) {
        if (mSession.AcfFile_cai() != loaded->acfFile || !mSession.IsLoadedAircraft_cai())
            mSession.SetAircraft_cai(loaded->acfFile, loaded->displayName, true);
    }
}

// The user aircraft was (re)loaded.
void AnalysePage::OnAircraftLoaded_cai()
{
    Refresh_cai();
}

// Builds the groups of the current view when the data or the view changed.
void AnalysePage::RebuildView_cai()
{
    if (mBuiltRevision == mSession.Revision_cai() && mBuiltView == mView)
        return;
    mBuiltRevision = mSession.Revision_cai();
    mBuiltView     = mView;
    mGroups.clear();
    mDuplicateOf.clear();
    const auto& data = mSession.Data_cai();
    if (!data)
        return;
    // Controls of different types with the same commands / datarefs (the door handle as an
    // up/down and a left/right switch): the later ones are marked, sync sends them once.
    std::map<std::vector<std::string>, std::string> firstType;
    mDuplicateOf.resize(data->controls.size());
    for (std::size_t g = 0; g < data->controls.size(); ++g) {
        const analysis::TypeGroup& type = data->controls[g];
        mDuplicateOf[g].resize(type.items.size());
        for (std::size_t i = 0; i < type.items.size(); ++i) {
            std::vector<std::string> refs = type.items[i].refs;
            std::sort(refs.begin(), refs.end());
            const auto [it, inserted] = firstType.emplace(refs, type.type);
            if (!inserted && it->second != type.type)
                mDuplicateOf[g][i] = it->second;
        }
    }
    if (mView == View::Type) {
        for (std::size_t g = 0; g < data->controls.size(); ++g) {
            const analysis::TypeGroup& type = data->controls[g];
            ViewGroup group;
            group.key      = type.type;
            group.row      = analysis::CategoryLabel_cai(type.category);
            group.category = type.category;
            for (std::size_t i = 0; i < type.items.size(); ++i)
                group.entries.emplace_back(g, i);
            mGroups.push_back(std::move(group));
        }
    } else {
        std::map<std::string, ViewGroup> bySystem;
        for (std::size_t g = 0; g < data->controls.size(); ++g) {
            const analysis::TypeGroup& type = data->controls[g];
            for (std::size_t i = 0; i < type.items.size(); ++i) {
                const std::string system = analysis::SystemGroup_cai(type.items[i].refs);
                ViewGroup& group = bySystem[system];
                if (group.entries.empty()) {
                    group.key      = system;
                    group.row      = system.substr(0, system.find('/'));
                    group.category = type.category;
                } else if (group.category && *group.category != type.category) {
                    group.category.reset();             // mixed
                }
                group.entries.emplace_back(g, i);
            }
        }
        for (auto& [system, group] : bySystem)
            mGroups.push_back(std::move(group));
    }
    Recount_cai();
}

// Recounts shared / shareable of every group.
void AnalysePage::Recount_cai()
{
    const auto& data = mSession.Data_cai();
    for (ViewGroup& group : mGroups) {
        group.shareable = 0;
        group.shared    = 0;
        if (!data)
            continue;
        for (const auto& [g, i] : group.entries) {
            const analysis::Control& control = data->controls[g].items[i];
            if (!control.share)
                continue;
            ++group.shareable;
            group.shared += *control.share ? 1 : 0;
        }
    }
}

// True when the Show filter lets this control through.
bool AnalysePage::RowShown_cai(std::size_t group, std::size_t item) const
{
    const auto& data = mSession.Data_cai();
    if (!data)
        return false;
    const analysis::TypeGroup& type = data->controls[group];
    const analysis::Control& control = type.items[item];
    switch (mShow) {
        case Show::All:       return true;
        case Show::Shared:    return control.share.value_or(false);
        case Show::NotShared: return control.share.has_value() && !*control.share;
        case Show::Changed:   return mSession.IsNew_cai(analysis::ControlKey_cai(type.type, control.refs));
    }
    return true;
}

// The full description of a control, for its tooltip.
void AnalysePage::ui_row_tooltip_cai(std::size_t group, std::size_t item)
{
    const analysis::TypeGroup& type = mSession.Data_cai()->controls[group];
    const analysis::Control& control = type.items[item];
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
    ImGui::TextUnformatted(analysis::ControlName_cai(control, type.type).c_str());
    for (const std::string& ref : control.refs)
        ImGui::BulletText("%s", ref.c_str());
    ImGui::TextDisabled("%s - %s", type.type.c_str(), control.file.c_str());
    if (!control.share)
        ImGui::TextColored(ui::theme::kErrorText, "Owned by the aircraft's plugin: cannot be shared.");
    else
        ImGui::TextUnformatted(*control.share ? "Shared with the other pilot." : "Kept local: each pilot has his own.");
    if (group < mDuplicateOf.size() && item < mDuplicateOf[group].size() && !mDuplicateOf[group][item].empty())
        ImGui::TextColored(ui::theme::kChangedText, "Same commands as a %s control - sent once when sharing.",
                           mDuplicateOf[group][item].c_str());
    if (mSession.IsNew_cai(analysis::ControlKey_cai(type.type, control.refs)))
        ImGui::TextColored(ui::theme::kChangedText, "New since the last reading.");
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// Ticks every control of `group` (or none).
void AnalysePage::TickGroup_cai(const ViewGroup& group, bool share)
{
    for (const auto& [g, i] : group.entries)
        mSession.SetShare_cai(g, i, share);
}

// Draws the page.
void AnalysePage::ui_draw_cai()
{
    ui_top_bar_cai();
    ui_pick_and_read_cai();
    ui::ui_step_title_cai(3, "Review and pick what to share", false, mSession.Data_cai().has_value(),
                          "Ticked = shared with the other pilot. Untick what each pilot keeps for himself "
                          "(popups, lights, volumes, cabin items). Can't-sync controls belong to the aircraft's "
                          "own plugin and cannot be shared.");
    RebuildView_cai();

    const ImGuiStyle& style = ImGui::GetStyle();
    const float areaH = ImGui::GetContentRegionAvail().y - kBottomHeight - style.ItemSpacing.y;
    const float leftW = ImGui::GetContentRegionAvail().x - kShareCardWidth - style.ItemSpacing.x;
    ImGui::BeginChild("##review", ImVec2(leftW, areaH));
    if (!mSession.Data_cai()) {
        ui::ui_wrapped_text_cai(mSession.HasAircraft_cai()
                                    ? "Read the aircraft files (step 2) to list its controls."
                                    : "Pick an airplane first (step 1).",
                                &ui::theme::kTextDim);
    } else {
        ui_filter_box_cai();
        ui_toolbar_cai();
        ui_list_cai();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ui_share_card_cai(areaH);
    ui_bottom_row_cai();
    if (mAskExplain) {
        ImGui::OpenPopup(kPopupExplain);
        mAskExplain = false;
    }
    ui_popup_explain_cai();
}

// The shareability card and its "What does this mean?" button.
void AnalysePage::ui_share_card_cai(float height)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::theme::kPopupBg);
    ImGui::BeginChild("##share", ImVec2(kShareCardWidth, height), ImGuiChildFlags_Borders);
    if (const auto& data = mSession.Data_cai()) {
        if (ui::ui_button_cai(ICON_FA_QUESTION_CIRCLE " What does this mean?", ImVec2(-1.0f, 0.0f)))
            mAskExplain = true;
        ui::ui_shareability_card_cai(*data, true);
        std::size_t notUsed = 0;
        for (const auto& [type, count] : data->signals.notUsed)
            notUsed += count;
        ImGui::Separator();
        ImGui::TextDisabled("Not used (no control of their own): %zu", notUsed);
        if (!data->signals.pluginsFound.empty()) {
            std::string plugins;
            for (const std::string& name : data->signals.pluginsFound)
                plugins += (plugins.empty() ? "" : ", ") + name;
            ui::ui_wrapped_text_cai("Aircraft plugins: " + plugins, &ui::theme::kTextDim);
        }
    } else {
        ui::ui_wrapped_text_cai("Shareability appears here after reading the files.", &ui::theme::kTextDim);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// The popup that explains, per system, what can be shared.
void AnalysePage::ui_popup_explain_cai()
{
    ui::ui_popup_center_next_cai();
    if (!ImGui::BeginPopupModal(kPopupExplain, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
        return;
    const auto& data = mSession.Data_cai();
    if (data) {
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth() - 40.0f);
        ImGui::TextUnformatted(fmt::format("{}: {}", mSession.DisplayName_cai(), data->grades.overallReason).c_str());
        ImGui::TextDisabled("Predicted from the aircraft's files. A shared session will tell for sure.");
        ImGui::Separator();
        ImGui::BeginChild("##explain", ImVec2(ImGui::GetWindowWidth() - 40.0f, 330.0f));
        for (const analysis::Explanation& item : analysis::ExplainSharing_cai(*data)) {
            ImGui::TextUnformatted(item.title.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, ui::theme::kTextDim);
            ImGui::TextWrapped("%s", item.text.c_str());
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }
        ImGui::EndChild();
        ImGui::PopTextWrapPos();
    }
    if (ui::ui_button_cai("Close", ImVec2(160.0f, 36.0f)))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// The top bar: Home, aircraft name and path, help.
void AnalysePage::ui_top_bar_cai()
{
    if (ui::ui_button_cai(ICON_FA_HOME " Home", ImVec2(0.0f, 44.0f)) && mGoHome)
        mGoHome();
    ImGui::SameLine();
    const float textW = ImGui::GetContentRegionAvail().x - 30.0f;
    ImGui::BeginGroup();
    ui::ui_text_ellipsis_cai(mSession.HasAircraft_cai() ? mSession.DisplayName_cai() : std::string("No aircraft"), textW);
    if (mSession.HasAircraft_cai())
        ui::ui_text_ellipsis_cai(ShownAcfPath_cai(mSession.AcfFile_cai()), textW, &ui::theme::kTextDim);
    ImGui::EndGroup();
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 24.0f);
    ui::ui_help_marker_cai("Step 1 picks the airplane, step 2 reads its .acf and cockpit OBJ files and sorts every "
                           "knob, switch and lever into commands, datarefs or can't sync. Step 3 lets you choose "
                           "what is shared. Save writes <acf name>.shared_cockpit.json next to the .acf, and a copy in the "
                           "plugin's backups folder.");
}

// Steps 1 and 2 side by side.
void AnalysePage::ui_pick_and_read_cai()
{
    if (ImGui::BeginTable("##pickread", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ui_pick_cai();
        ImGui::TableNextColumn();
        ui_read_cai();
        ImGui::EndTable();
    }
}

// Step 1: loaded or other aircraft.
void AnalysePage::ui_pick_cai()
{
    ui::ui_step_title_cai(1, "Pick the airplane", mSession.HasAircraft_cai(), !mSession.HasAircraft_cai());
    const bool busy = Busy_cai();
    ImGui::BeginDisabled(busy);
    if (ImGui::RadioButton("Loaded aircraft", !mOtherAircraft)) {
        mOtherAircraft = false;
        Refresh_cai();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Other aircraft...", mOtherAircraft)) {
        mOtherAircraft = true;
        if (!mAcfListReady && !mFinder.Running_cai())
            mFinder.Start_cai(Globals::get_cai().aircraftFolder);
    }
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        ImGui::TextColored(ui::theme::kChangedText, "%s", mSession.Scanning_cai() ? ICON_FA_HOURGLASS_HALF " reading"
                                                                                   : ICON_FA_EXCLAMATION_TRIANGLE " save first");
        return;
    }
    if (!mOtherAircraft)
        return;
    if (auto found = mFinder.TakeResult_cai()) {
        mAcfList      = std::move(*found);
        mAcfListReady = true;
        LOG_INFO("{} aircraft found", mAcfList.size());
    }
    if (mFinder.Running_cai()) {
        ImGui::TextDisabled(ICON_FA_SEARCH " Searching... %zu folders", mFinder.FoldersSeen_cai());
        return;
    }
    const std::string preview = mSession.HasAircraft_cai() && !mSession.IsLoadedAircraft_cai()
                                    ? Utils::Utf8_cai(mSession.AcfFile_cai().filename())
                                    : fmt::format("Choose one of {} aircraft", mAcfList.size());
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##acf", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##filter", ICON_FA_SEARCH " filter", &mAcfFilter);
        const std::string filter = Utils::Lower_cai(mAcfFilter);
        for (const fs::path& acf : mAcfList) {
            const std::string shown = ShownAcfPath_cai(acf);
            if (!filter.empty() && !Utils::Contains_cai(Utils::Lower_cai(shown), filter))
                continue;
            if (ImGui::Selectable(shown.c_str(), acf == mSession.AcfFile_cai())) {
                mSession.SetAircraft_cai(acf, Utils::Utf8_cai(acf.stem()), false);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndCombo();
    }
}

// Step 2: read the files.
void AnalysePage::ui_read_cai()
{
    const auto& data = mSession.Data_cai();
    const bool scanning = mSession.Scanning_cai();
    ui::ui_step_title_cai(2, "Read the aircraft files", data.has_value() && !scanning,
                          mSession.HasAircraft_cai() && !data.has_value());
    if (scanning) {
        const analysis::Scanner& scanner = mSession.ScannerRef_cai();
        const std::string overlay = fmt::format("{} of {} files", scanner.FilesDone_cai(), scanner.FilesTotal_cai());
        ImGui::ProgressBar(scanner.Fraction_cai(), ImVec2(ImGui::GetContentRegionAvail().x - 80.0f, 0.0f), overlay.c_str());
        ImGui::SameLine();
        if (ui::ui_color_button_cai(ICON_FA_STOP " Stop", ImVec2(-1.0f, 0.0f), ui::theme::kError))
            mSession.AbortScan_cai();
        return;
    }
    const char* label = data ? ICON_FA_SYNC " Read again" : ICON_FA_FOLDER_OPEN " Read aircraft files";
    const ImVec4& color = data ? ui::theme::kButton : ui::theme::kChanged;
    if (ui::ui_color_button_cai(label, ImVec2(0.0f, 0.0f), color, mSession.HasAircraft_cai()))
        mSession.StartScan_cai();
    ImGui::SameLine();
    if (mSession.CanRestore_cai() && !data) {
        if (ui::ui_button_cai(ICON_FA_HISTORY " Restore backup"))
            mSession.Restore_cai();
        return;
    }
    if (data) {
        std::size_t manipulators = 0;
        for (const analysis::FileEntry& file : data->files)
            manipulators += file.manipulators;
        if (mSession.AircraftChanged_cai())
            ImGui::TextColored(ui::theme::kChangedText, ICON_FA_EXCLAMATION_TRIANGLE " The aircraft changed - read again");
        else
            ImGui::TextColored(ui::theme::kSavedText, ICON_FA_CHECK " %zu files, %zu click areas", data->files.size(),
                               manipulators);
        // The seats: the vrconfig file's teleport hotspots (read again with the analysis).
        if (mSeatsFor != mSession.AcfFile_cai() || mSeatsRevision != mSession.Revision_cai()) {
            mSeatsFor = mSession.AcfFile_cai();
            mSeatsRevision = mSession.Revision_cai();
            mSeatNames.clear();
            for (const analysis::VrSeat& seat : analysis::ReadVrSeats_cai(mSeatsFor))
                mSeatNames.push_back(seat.name);
        }
        ImGui::SameLine();
        if (mSeatNames.empty())
            ImGui::TextDisabled(ICON_FA_CHAIR " no seats");
        else
            ImGui::TextDisabled(ICON_FA_CHAIR " %zu seats", mSeatNames.size());
        if (ImGui::IsItemHovered()) {
            std::string tip = mSeatNames.empty() ? std::string("No seats: the aircraft's vrconfig file has no teleport hotspots.")
                                                 : std::string("Seats (from the aircraft's vrconfig file):");
            for (const std::string& name : mSeatNames)
                tip += "\n  " + name;
            tip += "\n\nThe crew picks a seat on the Flight deck. New seats: make them with\nthe vrconfig_helper plugin, then Read again.";
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
}

// The filter box: one row per category (type view) or namespace (system view).
void AnalysePage::ui_filter_box_cai()
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float pillH  = ImGui::GetTextLineHeight() + kPillPadY * 2.0f;
    const float height = (pillH + 4.0f) * kFilterRows + 8.0f;
    std::set<std::string>& hidden = mHidden[static_cast<int>(mView)];

    std::vector<std::string> rows;
    for (const ViewGroup& group : mGroups) {
        if (std::find(rows.begin(), rows.end(), group.row) == rows.end())
            rows.push_back(group.row);
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::theme::kPopupBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
    ImGui::BeginChild("##filters", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, kPillPadY));
    const float startX = ImGui::GetCursorPosX();
    for (const std::string& row : rows) {
        ImGui::PushID(row.c_str());
        std::size_t total = 0;
        bool anyShown = false;
        for (const ViewGroup& group : mGroups) {
            if (group.row != row)
                continue;
            total += group.entries.size();
            anyShown = anyShown || !hidden.contains(group.key);
        }
        // The category of the row's groups (type view: all the same).
        auto lmbda_rowCategory = [&]() -> std::optional<Category> {
            for (const ViewGroup& group : mGroups)
                if (group.row == row) return group.category;
            return std::nullopt;
        };
        const ImVec4& rowColor = mView == View::Type ? CategoryColor_cai(lmbda_rowCategory()) : NamespaceColor_cai(row);
        const std::string rowLabel = fmt::format("{} {} {}", anyShown ? ICON_FA_CARET_DOWN : ICON_FA_CARET_RIGHT, row, total);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        if (ui::ui_color_button_cai(rowLabel.c_str(), ImVec2(kRowLabelWidth, 0.0f), anyShown ? rowColor : ui::theme::kButtonOff)) {
            for (const ViewGroup& group : mGroups) {
                if (group.row != row)
                    continue;
                if (anyShown) hidden.insert(group.key); else hidden.erase(group.key);
            }
        }
        ImGui::PopStyleVar();
        bool first = true;
        for (const ViewGroup& group : mGroups) {
            if (!anyShown)                              // a switched-off row is folded: no pills
                break;
            if (group.row != row)
                continue;
            const bool shown   = !hidden.contains(group.key);
            const bool partial = group.shareable > 0 && group.shared != group.shareable;
            const std::string shortName = mView == View::System && group.key.find('/') != std::string::npos
                                              ? group.key.substr(group.key.find('/') + 1)
                                              : group.key;
            const std::string count = partial ? fmt::format("{}/{}", group.shared, group.shareable)
                                              : std::to_string(group.entries.size());
            const std::string label = fmt::format("{}  {}##pill_{}", shortName, count, group.key);
            const float width = ImGui::CalcTextSize(fmt::format("{}  {}", shortName, count).c_str()).x + style.FramePadding.x * 2.0f;
            ImGui::SameLine();
            if (!first && ImGui::GetContentRegionAvail().x < width) {       // no room left on this line
                ImGui::NewLine();
                ImGui::SetCursorPosX(startX + kRowLabelWidth + style.ItemSpacing.x);
            }
            first = false;
            const ImVec4 bg   = !shown ? ui::theme::kPanelBgDark : partial ? ui::theme::kChanged : ui::theme::kInputBg;
            const ImVec4 text = !shown ? ui::theme::kTextDim : partial ? ui::theme::kTextOnColor : ui::theme::kText;
            ImGui::PushStyleColor(ImGuiCol_Button, bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::theme::kButtonHover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui::theme::kButtonActive);
            ImGui::PushStyleColor(ImGuiCol_Text, text);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
            if (ImGui::Button(label.c_str())) {
                if (shown) hidden.insert(group.key); else hidden.erase(group.key);
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// Group by, All, None, Fold all.
void AnalysePage::ui_toolbar_cai()
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Group by");
    ImGui::SameLine();
    if (ui::ui_color_button_cai("Type", ImVec2(0.0f, 0.0f), mView == View::Type ? ui::theme::kButtonActive : ui::theme::kButtonOff))
        mView = View::Type;
    ImGui::SameLine(0.0f, 2.0f);
    if (ui::ui_color_button_cai("System", ImVec2(0.0f, 0.0f), mView == View::System ? ui::theme::kButtonActive : ui::theme::kButtonOff))
        mView = View::System;
    ImGui::SameLine();
    // Each choice with how many controls it shows: "Not shared (14)".
    static constexpr const char* kShowNames[] = {"All", "Shared", "Not shared", "New"};
    std::size_t showCounts[4] = {0, 0, 0, 0};
    if (const auto& data = mSession.Data_cai()) {
        const Show keep = mShow;
        for (int choice = 0; choice < 4; ++choice) {
            mShow = static_cast<Show>(choice);
            for (std::size_t g = 0; g < data->controls.size(); ++g)
                for (std::size_t i = 0; i < data->controls[g].items.size(); ++i)
                    showCounts[choice] += RowShown_cai(g, i) ? 1 : 0;
        }
        mShow = keep;
    }
    std::string showLabels[4];
    for (int choice = 0; choice < 4; ++choice)
        showLabels[choice] = fmt::format("{} ({})", kShowNames[choice], showCounts[choice]);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("Not shared (0000)").x + ImGui::GetFrameHeight() + 16.0f);
    if (ImGui::BeginCombo("##show", showLabels[static_cast<int>(mShow)].c_str())) {
        for (int choice = 0; choice < 4; ++choice) {
            if (ImGui::Selectable(showLabels[choice].c_str(), choice == static_cast<int>(mShow)))
                mShow = static_cast<Show>(choice);
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show all controls, only shared ones, only those kept local (struck through),\n"
                          "or only those that are new since the last reading.");
    RebuildView_cai();

    std::set<std::string>& hidden = mHidden[static_cast<int>(mView)];
    std::set<std::string>& folded = mFolded[static_cast<int>(mView)];
    bool allFolded = true;
    for (const ViewGroup& group : mGroups) {
        if (!hidden.contains(group.key) && !folded.contains(group.key))
            allFolded = false;
    }
    const char* foldLabel = allFolded ? "Unfold all" : "Fold all";
    const ImGuiStyle& style = ImGui::GetStyle();
    const float buttonsW = ImGui::CalcTextSize("All").x + ImGui::CalcTextSize("None").x + ImGui::CalcTextSize(foldLabel).x +
                           style.FramePadding.x * 6.0f + style.ItemSpacing.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonsW);
    if (ui::ui_button_cai("All")) {
        for (const ViewGroup& group : mGroups)
            if (!hidden.contains(group.key)) TickGroup_cai(group, true);
    }
    ImGui::SameLine();
    if (ui::ui_button_cai("None")) {
        for (const ViewGroup& group : mGroups)
            if (!hidden.contains(group.key)) TickGroup_cai(group, false);
    }
    ImGui::SameLine();
    if (ui::ui_button_cai(foldLabel)) {
        for (const ViewGroup& group : mGroups) {
            if (hidden.contains(group.key))
                continue;
            if (allFolded) folded.erase(group.key); else folded.insert(group.key);
        }
    }
}

// The list of groups and controls (only the visible rows are drawn).
void AnalysePage::ui_list_cai()
{
    const auto& data = mSession.Data_cai();
    if (!data)
        return;
    const std::set<std::string>& hidden = mHidden[static_cast<int>(mView)];
    std::set<std::string>& folded = mFolded[static_cast<int>(mView)];

    // Header = item -1.
    struct Entry { std::size_t group; int item; };
    std::vector<Entry> entries;
    std::vector<std::size_t> matching(mGroups.size(), 0);   // per group: controls passing the Show filter
    for (std::size_t g = 0; g < mGroups.size(); ++g) {
        if (hidden.contains(mGroups[g].key))
            continue;
        std::vector<int> rows;
        for (std::size_t i = 0; i < mGroups[g].entries.size(); ++i) {
            const auto [dg, di] = mGroups[g].entries[i];
            if (RowShown_cai(dg, di))
                rows.push_back(static_cast<int>(i));
        }
        matching[g] = rows.size();
        if (rows.empty() && mShow != Show::All)
            continue;                                   // nothing of this group passes the Show filter
        entries.push_back({g, -1});
        if (!folded.contains(mGroups[g].key)) {
            for (int i : rows)
                entries.push_back({g, i});
        }
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::theme::kInputBg);
    ImGui::BeginChild("##list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    if (entries.empty())
        ui::ui_wrapped_text_cai(mShow == Show::All ? "Every group is hidden - pick some in the filter box above."
                                                   : "No control matches the Show filter.", &ui::theme::kTextDim);

    const float lineH = ImGui::GetTextLineHeight();
    const float rowH  = lineH * 2.0f + 8.0f;
    const float width = ImGui::GetContentRegionAvail().x;
    const float scroll = ImGui::GetScrollY();
    const float viewH  = ImGui::GetWindowHeight();
    const std::size_t first = static_cast<std::size_t>(std::max(0.0f, scroll / rowH));
    const std::size_t last  = std::min(entries.size(), static_cast<std::size_t>((scroll + viewH) / rowH) + 2);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    bool changed = false;

    for (std::size_t index = first; index < last; ++index) {
        const Entry& entry = entries[index];
        const ViewGroup& group = mGroups[entry.group];
        const float y = static_cast<float>(index) * rowH;
        ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, y + 4.0f));
        const ImVec2 rowMin(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + y - scroll);
        ImGui::PushID(static_cast<int>(index));
        if (entry.item < 0) {
            draw->AddRectFilled(rowMin, ImVec2(rowMin.x + ImGui::GetWindowWidth(), rowMin.y + rowH),
                                ImGui::ColorConvertFloat4ToU32(ui::theme::kPopupBg));
            const bool isFolded = folded.contains(group.key);
            ImGui::SetCursorPosY(y + (rowH - ImGui::GetFrameHeight()) * 0.5f);
            if (ImGui::ArrowButton("##fold", isFolded ? ImGuiDir_Right : ImGuiDir_Down)) {
                if (isFolded) folded.erase(group.key); else folded.insert(group.key);
            }
            ImGui::SameLine();
            const ui::TriState state = StateOf_cai(group.shared, group.shareable);
            ImGui::SetCursorPosY(y + (rowH - (ImGui::GetFrameHeight() - 4.0f)) * 0.5f);
            if (ui::ui_tristate_box_cai("##tick", state, group.shareable == 0)) {
                TickGroup_cai(group, state != ui::TriState::All);
                changed = true;
            }
            ImGui::SameLine();
            const float textY = y + (rowH - lineH) * 0.5f;
            ImGui::SetCursorPosY(textY);
            ui::ui_text_ellipsis_cai(group.key, std::max(40.0f, width * 0.55f));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%zu controls, %zu of %zu shareable ones shared", group.key.c_str(),
                                  group.entries.size(), group.shared, group.shareable);
            ImGui::SameLine();
            ImGui::SetCursorPosY(textY);
            static constexpr const char* kShowWord[] = {"", "shared", "not shared", "new"};
            if (mShow != Show::All)
                ImGui::TextColored(ui::theme::kChangedText, "(%zu %s of %zu)", matching[entry.group],
                                   kShowWord[static_cast<int>(mShow)], group.entries.size());
            else if (state == ui::TriState::Some)
                ImGui::TextColored(ui::theme::kChangedText, "(%zu/%zu)", group.shared, group.shareable);
            else
                ImGui::TextDisabled("(%zu)", group.entries.size());
            const char* chip = group.category ? analysis::CategoryLabel_cai(*group.category) : "Mixed";
            ImGui::SameLine(std::max(0.0f, width - ui::ChipWidth_cai(chip)));
            ImGui::SetCursorPosY(textY - 1.0f);
            ui::ui_chip_cai(chip, CategoryColor_cai(group.category));
        } else {
            const auto [g, i] = group.entries[static_cast<std::size_t>(entry.item)];
            const analysis::TypeGroup& type = data->controls[g];
            const analysis::Control& control = type.items[i];
            const bool locked = !control.share.has_value();
            const bool on     = control.share.value_or(false);
            ImGui::SetCursorPos(ImVec2(36.0f, y + (rowH - (ImGui::GetFrameHeight() - 4.0f)) * 0.5f));
            if (ui::ui_tristate_box_cai("##tick", on ? ui::TriState::All : ui::TriState::None, locked)) {
                mSession.SetShare_cai(g, i, !on);
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetCursorPosY(y + 4.0f);
            const float textX = ImGui::GetCursorPosX();
            const float tagW  = mView == View::System ? ImGui::CalcTextSize(type.type.c_str()).x + 12.0f : 0.0f;
            const float textW = width - textX - tagW;
            const std::string name = analysis::ControlName_cai(control, type.type);
            const ImVec4& nameColor = locked || !on ? ui::theme::kTextDim : ui::theme::kText;
            ui::ui_text_ellipsis_cai(name, textW, &nameColor);
            if (!locked && !on) {                       // struck through: kept local
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                draw->AddLine(ImVec2(min.x, (min.y + max.y) * 0.5f), ImVec2(max.x, (min.y + max.y) * 0.5f),
                              ImGui::ColorConvertFloat4ToU32(ui::theme::kTextDim), 1.0f);
            }
            ImGui::SetCursorPos(ImVec2(textX, y + 4.0f + lineH));
            std::string refs;
            for (const std::string& ref : control.refs)
                refs += (refs.empty() ? "" : "  -  ") + ref;
            ui::ui_text_ellipsis_cai(refs, textW, &ui::theme::kTextDim);
            if (tagW > 0.0f) {
                ImGui::SetCursorPos(ImVec2(width - tagW + 12.0f, y + (rowH - lineH) * 0.5f));
                ImGui::TextDisabled("%s", type.type.c_str());
            }
            // The whole row shows the full names when hovered.
            ImGui::SetCursorPos(ImVec2(textX, y));
            ImGui::InvisibleButton("##row", ImVec2(std::max(1.0f, width - textX), rowH - 2.0f));
            if (ImGui::IsItemHovered())
                ui_row_tooltip_cai(g, i);
            if (g < mDuplicateOf.size() && i < mDuplicateOf[g].size() && !mDuplicateOf[g][i].empty()) {
                const ImVec2 min = ImGui::GetItemRectMin();
                draw->AddText(ImVec2(min.x + width - textX - ImGui::CalcTextSize("duplicate").x - 4.0f, min.y + 4.0f),
                              ImGui::ColorConvertFloat4ToU32(ui::theme::kChangedText), "duplicate");
            }
            draw->AddLine(ImVec2(rowMin.x, rowMin.y + rowH - 1.0f), ImVec2(rowMin.x + ImGui::GetWindowWidth(), rowMin.y + rowH - 1.0f),
                          ImGui::ColorConvertFloat4ToU32(ui::theme::kListRowLine));
        }
        ImGui::PopID();
    }
    // The full height, so the scroll bar matches every row.
    ImGui::SetCursorPos(ImVec2(0.0f, static_cast<float>(entries.size()) * rowH));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::EndChild();
    ImGui::PopStyleColor();
    if (changed)
        Recount_cai();
}

// The bottom row: status strip, Save, Discard, Next.
void AnalysePage::ui_bottom_row_cai()
{
    const auto& data = mSession.Data_cai();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float saveW    = ImGui::CalcTextSize(ICON_FA_SAVE " Save").x + style.FramePadding.x * 4.0f;
    const float discardW = ImGui::CalcTextSize(ICON_FA_UNDO " Discard").x + style.FramePadding.x * 4.0f;
    const float nextW    = ImGui::CalcTextSize("Next: Connect " ICON_FA_ARROW_RIGHT).x + style.FramePadding.x * 4.0f;
    const float stripW   = ImGui::GetContentRegionAvail().x - saveW - discardW - nextW - style.ItemSpacing.x * 3.0f;

    std::string text;
    ImVec4 bg = ui::theme::kPanelBgDark, fg = ui::theme::kText;
    const std::string counts = data ? fmt::format("sharing {} of {} controls", data->SharedCount_cai(), data->ShareableCount_cai())
                                    : std::string();
    if (!mSession.LastError_cai().empty()) {
        text = ICON_FA_EXCLAMATION_TRIANGLE " " + mSession.LastError_cai();
        bg = ui::theme::kError; fg = ui::theme::kTextOnColor;
    } else if (mSession.Scanning_cai()) {
        text = ICON_FA_HOURGLASS_HALF " Reading the aircraft files...";
    } else if (mSession.Dirty_cai()) {
        text = ICON_FA_CIRCLE " Not saved yet - " + counts;
        bg = ui::theme::kChanged; fg = ui::theme::kTextOnColor;
    } else if (!mSession.LastNotice_cai().empty()) {
        text = ICON_FA_CHECK " " + mSession.LastNotice_cai() + (counts.empty() ? "" : " - " + counts);
        bg = ui::theme::kSaved; fg = ui::theme::kTextOnColor;
    } else if (data && mSession.OnDisk_cai()) {
        text = ICON_FA_CHECK " Saved - " + counts;
    } else {
        text = "No analysis yet";
    }
    ui::ui_status_strip_cai(text, bg, fg, stripW, kBottomHeight);
    if (!mSession.ScanProblems_cai().empty() && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Notes from the last reading:");
        for (std::size_t i = 0; i < mSession.ScanProblems_cai().size() && i < 15; ++i)
            ImGui::BulletText("%s", mSession.ScanProblems_cai()[i].c_str());
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    if (ui::ui_color_button_cai(ICON_FA_SAVE " Save", ImVec2(saveW, kBottomHeight), ui::theme::kChanged,
                                mSession.Dirty_cai() && !mSession.Scanning_cai()))
        mSession.Save_cai();
    ImGui::SameLine();
    if (ui::ui_button_cai(ICON_FA_UNDO " Discard", ImVec2(discardW, kBottomHeight), mSession.Dirty_cai() && !mSession.Scanning_cai()))
        mSession.Revert_cai();
    ImGui::SameLine();
    ui::ui_button_cai("Next: Connect " ICON_FA_ARROW_RIGHT, ImVec2(nextW, kBottomHeight), false);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Connecting comes in a later version.");
}

} // namespace sc::pages
