#include "StrategyPanelOverview.h"

#include "StrategyPanelWidgets.h"

#include <Didrachma/strategy/core/Validation.h>
#include <algorithm>
#include <chrono>
#include <imgui.h>

namespace Didrachma::Apps::Studio::Intern {
void draw_strategy_overview(Didrachma::Studio::Core::Strategies& strategies, StrategyPanelState& state,
                            std::span<const Analysis::Core::Indicator::Definition> catalog,
                            const DraftTransitionRequest& request_transition,
                            std::function<StockChart::Core::Document&(const Market::Core::Series::Key&)> open_chart,
                            std::function<StockChart::Core::Document*(std::string_view)> find_chart) {
    if (!ImGui::Begin("Strategies")) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("strategy-views")) {
        if (ImGui::BeginTabItem("Definitions")) {
            for (const auto& record : strategies.definitions())
                if (ImGui::Selectable(record.definition.display_name.c_str(),
                                      state.selected_definition == record.definition.id))
                    request_transition(
                        {Didrachma::Studio::Core::DraftTransitionKind::SwitchDefinition, record.definition.id});
            text_input("Import from", state.import_path);
            if (ImGui::Button("Create"))
                request_transition({Didrachma::Studio::Core::DraftTransitionKind::Create, {}});
            ImGui::SameLine();
            if (ImGui::Button("Load / import"))
                request_transition({Didrachma::Studio::Core::DraftTransitionKind::Import, state.import_path});
            const auto selected = std::ranges::find(strategies.definitions(), state.selected_definition,
                                                    [](const auto& item) { return item.definition.id; });
            ImGui::SameLine();
            if (ImGui::Button("Duplicate") && !state.selected_definition.empty())
                request_transition(
                    {Didrachma::Studio::Core::DraftTransitionKind::Duplicate, state.selected_definition});
            ImGui::SameLine();
            if (ImGui::Button("Edit") && !state.selected_definition.empty())
                request_transition(
                    {Didrachma::Studio::Core::DraftTransitionKind::SwitchDefinition, state.selected_definition});
            ImGui::SameLine();
            if (ImGui::Button("Delete") && !state.selected_definition.empty())
                state.confirm_delete = true;
            const bool dirty_editor =
                state.editor && state.editor->dirty() && state.edited_definition == state.selected_definition;
            const bool valid = selected != strategies.definitions().end() && !selected->unsaved && !dirty_editor &&
                               Strategy::Core::validate(selected->definition, catalog).empty();
            ImGui::BeginDisabled(!valid);
            if (ImGui::Button("New backtest")) {
                Didrachma::Studio::Core::BacktestSetupValues recent;
                Didrachma::Studio::Core::RecentBacktestSetupRepository repository{"didrachma-recent-backtest.json"};
                if (auto loaded = repository.load();
                    std::holds_alternative<Didrachma::Studio::Core::BacktestSetupValues>(loaded))
                    recent = std::get<Didrachma::Studio::Core::BacktestSetupValues>(std::move(loaded));
                state.backtest_setup = strategies.new_backtest(state.selected_definition, catalog, std::move(recent));
                state.backtest_feedback.clear();
                state.backtest_input_fingerprint.clear();
            }
            ImGui::EndDisabled();
            if (!valid && selected != strategies.definitions().end())
                for (const auto& error : Strategy::Core::validate(selected->definition, catalog))
                    ImGui::TextColored({1, .4F, .4F, 1}, "%s: %s", error.path.c_str(), error.message.c_str());
            if (dirty_editor)
                ImGui::TextColored({1, .4F, .4F, 1}, "Save or discard editor changes before backtesting");
            ImGui::TextDisabled("Historical backtests are configured before submission. Live polling is separate.");
            for (const auto& error : state.validation_errors)
                ImGui::TextColored({1, .4F, .4F, 1}, "%s: %s", error.path.c_str(), error.message.c_str());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Running")) {
            for (const auto& run : strategies.runs())
                if (ImGui::Selectable(run.id.c_str(), state.selected_run == run.id))
                    state.selected_run = run.id;
            if (ImGui::Button("Stop selected") && !state.selected_run.empty())
                state.confirm_stop = true;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (state.confirm_delete) {
        ImGui::OpenPopup("Delete strategy?");
        state.confirm_delete = false;
    }
    if (ImGui::BeginPopupModal("Delete strategy?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Unsaved changes will be lost.");
        if (ImGui::Button("Delete")) {
            request_transition({Didrachma::Studio::Core::DraftTransitionKind::Delete, state.selected_definition});
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (state.confirm_stop) {
        ImGui::OpenPopup("Stop strategy run?");
        state.confirm_stop = false;
    }
    if (ImGui::BeginPopupModal("Stop strategy run?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::Button("Stop")) {
            strategies.stop(state.selected_run,
                            std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()),
                            std::move(find_chart));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}
} // namespace Didrachma::Apps::Studio::Intern
