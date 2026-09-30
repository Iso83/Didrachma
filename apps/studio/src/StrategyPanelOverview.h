#pragma once

#include "StrategyPanel.h"

namespace Didrachma::Apps::Studio::Intern {
using DraftTransitionRequest = std::function<void(Didrachma::Studio::Core::DraftTransition)>;

void draw_strategy_overview(Didrachma::Studio::Core::Strategies&, StrategyPanelState&,
                            std::span<const Analysis::Core::Indicator::Definition>, const DraftTransitionRequest&,
                            std::function<StockChart::Core::Document&(const Market::Core::Series::Key&)>,
                            std::function<StockChart::Core::Document*(std::string_view)>);
} // namespace Didrachma::Apps::Studio::Intern
