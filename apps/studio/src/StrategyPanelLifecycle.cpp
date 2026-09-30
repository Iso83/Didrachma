#include "StrategyPanelLifecycle.h"

#include "StrategyPanelWidgets.h"

#include <algorithm>
#include <imgui.h>

namespace Didrachma::Apps::Studio::Intern {
void draw_unsaved_transition(Didrachma::Studio::Core::Strategies& strategies, StrategyPanelState& state,
                             std::span<const Analysis::Core::Indicator::Definition> catalog,
                             const std::function<void(const Didrachma::Studio::Core::DraftTransition&)>& execute) {
    if (state.draft_transition.pending())
        ImGui::OpenPopup("Unsaved strategy changes");
    if (!ImGui::BeginPopupModal("Unsaved strategy changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextUnformatted("Save changes before leaving this strategy?");
    const auto show_errors = [&] {
        for (const auto& message : state.messages)
            ImGui::TextColored({1, .4F, .4F, 1}, "%s", message.c_str());
        for (const auto& error : state.validation_errors)
            ImGui::TextColored({1, .4F, .4F, 1}, "%s: %s", error.path.c_str(), error.message.c_str());
    };
    const auto record = std::ranges::find(strategies.definitions(), state.edited_definition,
                                          [](const auto& item) { return item.definition.id; });
    const auto finish = [&](std::optional<Didrachma::Studio::Core::DraftTransition> transition) {
        if (!transition)
            return;

        state.editor.reset();
        state.editor_open = false;
        state.edited_definition.clear();
        execute(*transition);
    };
    if (ImGui::Button("Save")) {
        const auto decision =
            state.draft_transition.decide(Didrachma::Studio::Core::DraftDecision::Save,
                                          record != strategies.definitions().end() && record->path.has_value());
        if (decision == Didrachma::Studio::Core::DraftDecisionResult::SaveAsRequired) {
            state.messages = {"This definition has not been saved. Enter a Save As path to continue."};
        } else if (decision == Didrachma::Studio::Core::DraftDecisionResult::SaveThenProceed) {
            const auto errors = strategies.save_draft(state.edited_definition, *state.editor, *record->path, catalog);
            state.validation_errors = errors;
            state.messages.clear();
            if (errors.empty()) {
                finish(state.draft_transition.complete_transition());
                ImGui::CloseCurrentPopup();
            }
        }
    }
    if (record == strategies.definitions().end() || !record->path) {
        text_input("Save as path", state.save_path);
        if (ImGui::Button("Save As and proceed") && !state.save_path.empty()) {
            const auto errors = strategies.save_draft(state.edited_definition, *state.editor, state.save_path, catalog);
            state.validation_errors = errors;
            state.messages.clear();
            if (errors.empty()) {
                finish(state.draft_transition.complete_transition());
                ImGui::CloseCurrentPopup();
            }
        }
    }
    show_errors();
    ImGui::SameLine();
    if (ImGui::Button("Discard")) {
        state.editor->cancel();
        state.draft_transition.decide(Didrachma::Studio::Core::DraftDecision::Discard,
                                      record != strategies.definitions().end() && record->path.has_value());
        finish(state.draft_transition.complete_transition());
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Continue editing")) {
        state.draft_transition.decide(Didrachma::Studio::Core::DraftDecision::ContinueEditing, false);
        state.editor_open = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
} // namespace Didrachma::Apps::Studio::Intern
