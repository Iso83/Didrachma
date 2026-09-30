#include <Didrachma/studio/core/StrategyEditor.h>
#include <utility>

namespace Didrachma::Studio::Core {
bool DraftTransitionGuard::request(DraftTransition transition, bool dirty) {
    if (!dirty)
        return true;

    m_pending = std::move(transition);
    return false;
}

DraftDecisionResult DraftTransitionGuard::decide(DraftDecision decision, bool has_existing_path) {
    if (!m_pending || decision == DraftDecision::ContinueEditing) {
        m_pending.reset();
        return DraftDecisionResult::ContinueEditing;
    }

    if (decision == DraftDecision::Discard)
        return DraftDecisionResult::Proceed;

    return has_existing_path ? DraftDecisionResult::SaveThenProceed : DraftDecisionResult::SaveAsRequired;
}

std::optional<DraftTransition> DraftTransitionGuard::complete_transition() {
    return std::exchange(m_pending, std::nullopt);
}

DraftRequestResult request_draft_transition(DraftTransitionGuard& guard, DraftTransition transition,
                                            std::string_view edited_definition, bool dirty) {
    if (transition.kind == DraftTransitionKind::SwitchDefinition && transition.definition_id == edited_definition)
        return DraftRequestResult::SameDefinition;

    return guard.request(std::move(transition), dirty) ? DraftRequestResult::Proceed
                                                       : DraftRequestResult::DecisionRequired;
}
} // namespace Didrachma::Studio::Core
