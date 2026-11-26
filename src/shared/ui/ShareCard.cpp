// shared_cockpit - the shareability card (overall grade + one row per system)

#include "shared/ui/ShareCard.h"

#include <algorithm>
#include <string>

#include "shared/ui/Widgets.h"

namespace sc::ui {

namespace {

// The colour of a grade chip.
const ImVec4& GradeColor_cai(analysis::Grade grade)
{
    switch (grade) {
        case analysis::Grade::Full:    return theme::kGradeFull;
        case analysis::Grade::Mostly:  return theme::kGradeMostly;
        case analysis::Grade::Partial: return theme::kGradePartial;
        case analysis::Grade::None:    return theme::kGradeNone;
        case analysis::Grade::Check:   return theme::kGradeCheck;
    }
    return theme::kGradeCheck;
}

// The colour of the overall grade chip.
const ImVec4& OverallColor_cai(analysis::Overall overall)
{
    switch (overall) {
        case analysis::Overall::Good:    return theme::kGradeFull;
        case analysis::Overall::Limited: return theme::kGradePartial;
        case analysis::Overall::Poor:    return theme::kGradeNone;
    }
    return theme::kGradeCheck;
}

} // namespace

// Draws the shareability card: the overall grade, then one row per system.
void ui_shareability_card_cai(const analysis::AnalysisData& data, bool withReasons)
{
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted("Shareability");
    ImGui::SameLine(width - ImGui::CalcTextSize(data.grades.source.c_str()).x);
    ImGui::TextDisabled("%s", data.grades.source.c_str());
    ImGui::Separator();
    ui_chip_cai(analysis::OverallLabel_cai(data.grades.overall), OverallColor_cai(data.grades.overall));
    ImGui::SameLine();
    // "Not fully: ..." on a line of its own (files saved before 0.1.6 have it on the same line).
    std::string reason = data.grades.overallReason;
    if (const std::size_t at = reason.find(". Not fully:"); at != std::string::npos)
        reason.replace(at, 2, ".\n");
    ImGui::BeginGroup();
    ui_wrapped_text_cai(reason);
    ImGui::EndGroup();
    ImGui::Separator();
    // The chips in a fixed column on the left, as wide as the widest grade: they never cover a name.
    const float chipColumn = ChipWidth_cai(analysis::GradeLabel_cai(analysis::Grade::Check)) + 8.0f;
    for (const analysis::SystemGrade& system : data.grades.systems) {
        const char* grade = analysis::GradeLabel_cai(system.grade);
        const float x = ImGui::GetCursorPosX();
        ui_chip_cai(grade, GradeColor_cai(system.grade));
        ImGui::SameLine(x + chipColumn);
        ui_text_ellipsis_cai(analysis::SystemLabel_cai(system.system), std::max(10.0f, width - chipColumn));
        if (withReasons) {
            ImGui::SetCursorPosX(x + chipColumn);
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDim);
            ImGui::PushTextWrapPos(x + width);
            ImGui::TextUnformatted(system.reason.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
    }
}

} // namespace sc::ui
