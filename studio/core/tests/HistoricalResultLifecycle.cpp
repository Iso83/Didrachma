#include "TestAssert.h"

#include <Didrachma/studio/core/HistoricalResults.h>
#include <condition_variable>

using namespace Didrachma;

namespace {
using Timestamp = Market::Core::Time::UtcTimestamp;

Timestamp at(std::int64_t seconds) {
    return Timestamp{std::chrono::seconds{seconds}};
}

Strategy::Core::BacktestRequest request() {
    Strategy::Core::Definition definition;
    definition.id = "immutable";
    definition.display_name = "Submitted snapshot";
    return {definition, "AAPL", at(100), at(200), {"fixture", {}}, {3, 1000, 1, .02, .03}, 7};
}

Strategy::Core::BacktestOutcome completed_with_projection(const Strategy::Core::BacktestRequest& value) {
    Strategy::Core::BacktestOutcome outcome;
    outcome.request = value;
    outcome.status = Strategy::Core::BacktestStatus::Completed;
    outcome.result_status = Strategy::Core::BacktestResultStatus::Exited;
    const Market::Core::Series::Key key{"fixture", "AAPL", {1, Market::Core::Time::Unit::Day}};
    outcome.inputs.push_back({{"primary", key}, {Strategy::Core::Readiness::Ready}, {}});
    Strategy::Core::RunResult result;
    result.entry_time = at(1200);
    result.exit_time = at(1800);
    result.entry_price = 100.0;
    result.exit_price = 105.0;
    result.projection.markers = {{Strategy::Core::ProjectionKind::EntryMarker, *result.entry_time, 100.0},
                                 {Strategy::Core::ProjectionKind::ExitMarker, *result.exit_time, 105.0}};
    outcome.occurrences.push_back({0, Strategy::Core::BacktestResultStatus::Exited, std::move(result)});
    return outcome;
}

void apply_until_finished(Studio::Core::HistoricalBacktests& runs) {
    while (!runs.runs().back().outcome) {
        runs.apply_pending();
        std::this_thread::yield();
    }
}

int test_historical_result_cleanup_lifecycle() {
    std::mutex mutex;
    std::condition_variable entered;
    bool fourth_running{};
    std::size_t invocation{};
    Studio::Core::HistoricalBacktests runs{[&](const auto& value, const auto& cancelled, const auto&) {
        std::size_t current;
        {
            std::scoped_lock lock{mutex};
            current = ++invocation;
            if (current == 4)
                fourth_running = true;
        }
        entered.notify_one();
        while (current == 4 && !cancelled())
            std::this_thread::yield();
        auto outcome = completed_with_projection(value);
        if (current == 2)
            outcome.status = Strategy::Core::BacktestStatus::Failed;
        if (current == 3)
            outcome.status = Strategy::Core::BacktestStatus::Cancelled;

        return outcome;
    }};
    const auto first_id = runs.enqueue(request());
    apply_until_finished(runs);
    const auto second_id = runs.enqueue(request());
    apply_until_finished(runs);
    const auto third_id = runs.enqueue(request());
    apply_until_finished(runs);
    CPPTEST_ASSERT(first_id == "backtest-1" && second_id == "backtest-2" && third_id == "backtest-3");

    Studio::Core::Workspace workspace;
    Studio::Core::HistoricalChartIntegration charts;
    const auto key = runs.runs().front().outcome->inputs.front().series.key;
    Studio::Core::HistoricalTimelineItem item;
    item.time = at(1200);
    item.source_key = key;
    item.source_timeframe = key.timeframe;
    const auto second_chart = charts.select(workspace, runs.runs()[1], item);
    item.time = at(-1000000000);
    const auto first_chart = charts.select(workspace, runs.runs()[0], item);
    CPPTEST_ASSERT(workspace.documents().size() == 1);
    CPPTEST_ASSERT(second_chart.document == &workspace.documents().front());
    CPPTEST_ASSERT(charts.overlays_for(first_chart.document->id()).size() == 2);
    const auto indicator = first_chart.document->add_indicator("manual-indicator", {});
    StockChart::Core::Profile profile;
    profile.name = "Manual profile";
    CPPTEST_ASSERT(!workspace.profiles().add(profile));
    const auto* history_before = workspace.history_request(first_chart.document->id());
    CPPTEST_ASSERT(history_before);
    const auto history_range = history_before->range;

    std::string selected = first_id;
    std::string submitted = first_id;
    const auto removed = Studio::Core::remove_historical_result(runs, charts, first_id, selected, submitted);
    CPPTEST_ASSERT(removed.status == Studio::Core::HistoricalRemovalStatus::Removed && removed.count == 1);
    CPPTEST_ASSERT(runs.runs().size() == 2 && runs.runs().front().id == second_id && runs.runs().back().id == third_id);
    CPPTEST_ASSERT(selected == third_id && submitted.empty());
    CPPTEST_ASSERT(charts.overlays_for(first_chart.document->id()).size() == 1 &&
                   charts.overlays_for(first_chart.document->id()).front()->run_id == second_id);
    const auto stale_navigation = charts.history_loaded(*first_chart.document, {at(1200), at(1800)}, history_range);
    CPPTEST_ASSERT(!stale_navigation.applied);
    CPPTEST_ASSERT(workspace.documents().size() == 1 && first_chart.document->find_indicator(indicator) &&
                   workspace.profiles().find("Manual profile") &&
                   workspace.history_request(first_chart.document->id())->range.begin == history_range.begin &&
                   workspace.history_request(first_chart.document->id())->range.end == history_range.end);

    const auto active_id = runs.enqueue(request());
    {
        std::unique_lock lock{mutex};
        entered.wait(lock, [&] { return fourth_running; });
    }
    selected = active_id;
    submitted = second_id;
    const auto refused = Studio::Core::remove_historical_result(runs, charts, active_id, selected, submitted);
    CPPTEST_ASSERT(refused.status == Studio::Core::HistoricalRemovalStatus::Active && refused.count == 0);
    const auto cleared = Studio::Core::clear_finished_historical_results(runs, charts, selected, submitted);
    CPPTEST_ASSERT(cleared.count == 2 && runs.runs().size() == 1 && runs.runs().front().id == active_id);
    CPPTEST_ASSERT(selected == active_id && submitted.empty() &&
                   charts.overlays_for(first_chart.document->id()).empty());
    CPPTEST_ASSERT(workspace.documents().size() == 1 && first_chart.document->find_indicator(indicator) &&
                   workspace.profiles().find("Manual profile"));
    runs.cancel();
    const auto next_id = runs.enqueue(request());
    CPPTEST_ASSERT(next_id == "backtest-5");
    runs.cancel();
    return 0;
}

int test_occurrence_overlays_are_independent_and_hoverable() {
    auto value = request();
    value.strategy_snapshot.primary_series_id = "primary";
    value.strategy_snapshot.display_name = "Overlapping strategy";
    Studio::Core::HistoricalBacktest run{"backtest-overlap", value};
    run.outcome.emplace();
    const Market::Core::Series::Key key{"fixture", "AAPL", {1, Market::Core::Time::Unit::Minute}};
    run.outcome->inputs.push_back({{"primary", key}, {Strategy::Core::Readiness::Ready}, {}});
    auto occurrence = [](std::uint64_t id, Timestamp entry, Timestamp exit, double entry_price, double exit_price,
                         double stop, double target) {
        Strategy::Core::RunResult result;
        result.entry_time = entry;
        result.exit_time = exit;
        result.entry_price = entry_price;
        result.exit_price = exit_price;
        result.exit_reason =
            exit_price >= entry_price ? Strategy::Core::ExitReason::Target : Strategy::Core::ExitReason::StopLoss;
        result.gross_profit_loss = exit_price - entry_price;
        result.net_profit_loss = result.gross_profit_loss - 1.0;
        result.projection.markers = {{Strategy::Core::ProjectionKind::EntryMarker, entry, entry_price},
                                     {Strategy::Core::ProjectionKind::ExitMarker, exit, exit_price}};
        result.projection.segments = {{Strategy::Core::ProjectionKind::StopPrice, entry, exit, stop},
                                      {Strategy::Core::ProjectionKind::TargetPrice, entry, exit, target}};
        return Strategy::Core::BacktestOccurrence{id, Strategy::Core::BacktestResultStatus::Exited, result};
    };
    run.outcome->occurrences.push_back(occurrence(11, at(120), at(240), 10, 13, 9, 13));
    run.outcome->occurrences.push_back(occurrence(12, at(180), at(300), 20, 18, 18, 24));

    Studio::Core::Workspace workspace;
    Studio::Core::HistoricalChartIntegration charts;
    Studio::Core::HistoricalTimelineItem item;
    item.time = at(120);
    item.source_key = key;
    item.occurrence_id = 11;
    const auto selection = charts.select(workspace, run, item);
    CPPTEST_ASSERT(selection.document && charts.attachments().size() == 2);
    CPPTEST_ASSERT(charts.overlays_for(selection.document->id()).size() == 1);
    CPPTEST_ASSERT(charts.set_enabled(selection.document->id(), run.id, 12, key, true));
    const auto overlays = charts.overlays_for(selection.document->id());
    CPPTEST_ASSERT(overlays.size() == 2 && overlays[0]->position_span->begin == at(120) &&
                   overlays[0]->position_span->end == at(240) && overlays[0]->profitable &&
                   overlays[1]->position_span->begin == at(180) && overlays[1]->position_span->end == at(300) &&
                   !overlays[1]->profitable);
    const auto hovered = charts.hover_values(selection.document->id(), at(200));
    CPPTEST_ASSERT(hovered.size() == 2 && hovered[0].overlay->occurrence_id == 11 && hovered[0].stop == 9 &&
                   hovered[0].target == 13 && hovered[1].overlay->occurrence_id == 12 && hovered[1].stop == 18 &&
                   hovered[1].target == 24);
    CPPTEST_ASSERT(charts.set_enabled(selection.document->id(), run.id, 11, key, false));
    CPPTEST_ASSERT(charts.overlays_for(selection.document->id()).size() == 1 &&
                   charts.hover_values(selection.document->id(), at(200)).front().overlay->occurrence_id == 12);
    CPPTEST_ASSERT(charts.remove_run(run.id) && charts.attachments().empty() &&
                   charts.overlays_for(selection.document->id()).empty());
    return 0;
}

int test_evidence_presentation_uses_explicit_occurrence_ownership() {
    auto value = request();
    value.strategy_snapshot.primary_series_id = "primary";
    Studio::Core::HistoricalBacktest run{"evidence-run", value};
    run.outcome.emplace();
    const Market::Core::Series::Key key{"fixture", "ACME", {1, Market::Core::Time::Unit::Minute}};
    run.outcome->inputs.push_back({{"primary", key}, {Strategy::Core::Readiness::Ready}, {}});

    Strategy::Core::Evidence evidence;
    evidence.condition_path = "entry/all";
    evidence.truth = Strategy::Core::Truth::True;
    evidence.binding_id = "primary";
    evidence.source_key = key;
    evidence.source_timeframe = key.timeframe;
    evidence.source_time = at(120);
    Strategy::Core::Evaluation evaluation{at(120), Strategy::Core::Truth::True, {evidence}};
    Strategy::Core::StrategyEvent event{Strategy::Core::StrategyEventKind::EntryArmed,
                                        at(120),
                                        at(120),
                                        Strategy::Core::RunState::WaitingForEntry,
                                        Strategy::Core::RunState::EntryArmed,
                                        {"entry"},
                                        {},
                                        {},
                                        {evidence},
                                        "same-looking event"};
    for (std::uint64_t id : {7, 8}) {
        Strategy::Core::RunResult result;
        result.entry_time = at(120 + static_cast<std::int64_t>(id));
        result.entry_price = 10.0;
        result.evaluations.push_back(evaluation);
        result.events.push_back(event);
        run.outcome->evaluations.push_back({id, evaluation});
        run.outcome->occurrences.push_back({id, Strategy::Core::BacktestResultStatus::SignalNotFilled, result});
        run.outcome->audit_events.push_back({id, event});
    }
    auto run_event = event;
    run_event.evidence.clear();
    run_event.effective_time = at(90);
    run.outcome->audit_events.push_back({std::nullopt, run_event});

    const auto items = Studio::Core::timeline(run);
    CPPTEST_ASSERT(items.size() == 5 && !items.front().occurrence_id && !items.front().navigable());
    CPPTEST_ASSERT(std::ranges::is_sorted(items, {}, &Studio::Core::HistoricalTimelineItem::time));
    CPPTEST_ASSERT(std::ranges::count(items, std::optional<std::uint64_t>{7},
                                      &Studio::Core::HistoricalTimelineItem::occurrence_id) == 2);
    CPPTEST_ASSERT(std::ranges::count(items, std::optional<std::uint64_t>{8},
                                      &Studio::Core::HistoricalTimelineItem::occurrence_id) == 2);
    CPPTEST_ASSERT(std::ranges::count_if(items, &Studio::Core::HistoricalTimelineItem::navigable) == 4);

    const auto groups = Studio::Core::evidence_groups(run);
    CPPTEST_ASSERT(groups.size() == 3 && groups[0].label == "Occurrence #1 (id 7)" &&
                   groups[1].label == "Occurrence #2 (id 8)" && groups[2].label == "Run-level evidence");
    CPPTEST_ASSERT(groups[0].items.size() == 2 && groups[1].items.size() == 2 && groups[2].items.size() == 1);
    return 0;
}
} // namespace

int main() {
    CPPTEST_RUN(test_historical_result_cleanup_lifecycle);
    CPPTEST_RUN(test_occurrence_overlays_are_independent_and_hoverable);
    CPPTEST_RUN(test_evidence_presentation_uses_explicit_occurrence_ownership);
    return 0;
}
