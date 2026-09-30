#pragma once

#include <Didrachma/studio/core/HistoricalBacktests.h>
#include <Didrachma/studio/core/HistoricalResults.h>
#include <Didrachma/studio/core/Strategies.h>
#include <Didrachma/studio/core/StrategyEditor.h>
#include <functional>

namespace Didrachma::Apps::Studio {
struct StrategyPanelState {
    std::string selected_definition;
    std::string selected_run;
    std::string subject;
    std::string import_path{"strategy.json"};
    std::string save_path{"strategy.json"};
    std::unique_ptr<Didrachma::Studio::Core::StrategyEditor> editor;
    Didrachma::Studio::Core::DraftTransitionGuard draft_transition;
    std::string edited_definition;
    std::vector<Strategy::Core::RepositoryError> validation_errors;
    std::vector<std::string> messages;
    std::unique_ptr<Didrachma::Studio::Core::BacktestSetup> backtest_setup;
    Didrachma::Studio::Core::HistoricalBacktests* historical_backtests{};
    Didrachma::Studio::Core::HistoricalChartIntegration historical_charts;
    std::string selected_historical_run;
    std::string submitted_historical_run;
    std::vector<std::string> backtest_feedback;
    std::string backtest_input_fingerprint;
    bool editor_open{};
    bool confirm_delete{};
    bool confirm_stop{};
};

void draw_backtest_setup(Didrachma::Studio::Core::Strategies&, StrategyPanelState&);

void draw_strategies_panel(Didrachma::Studio::Core::Strategies&, StrategyPanelState&,
                           std::span<const Analysis::Core::Indicator::Definition>,
                           std::function<StockChart::Core::Document&(const Market::Core::Series::Key&)> open_chart,
                           std::function<StockChart::Core::Document*(std::string_view)> find_chart);
void draw_strategy_monitor(
    Didrachma::Studio::Core::Strategies&, StrategyPanelState&,
    std::function<void(const Didrachma::Studio::Core::HistoricalBacktest&,
                       const Didrachma::Studio::Core::HistoricalTimelineItem&)>
        navigate_historical,
    std::function<void(std::string_view chart_id, std::optional<Market::Core::Time::UtcTimestamp>)> navigate);
} // namespace Didrachma::Apps::Studio
