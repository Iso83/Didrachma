#pragma once

#include "StrategyPanel.h"

namespace Didrachma::Apps::Studio::Intern {
void draw_unsaved_transition(Didrachma::Studio::Core::Strategies&, StrategyPanelState&,
                             std::span<const Analysis::Core::Indicator::Definition>,
                             const std::function<void(const Didrachma::Studio::Core::DraftTransition&)>&);
} // namespace Didrachma::Apps::Studio::Intern
