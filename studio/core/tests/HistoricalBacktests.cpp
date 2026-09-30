#include "TestAssert.h"

#include <Didrachma/strategy/core/Repository.h>
#include <Didrachma/studio/core/HistoricalBacktests.h>
#include <Didrachma/studio/core/HistoricalResults.h>
#include <algorithm>
#include <array>
#include <condition_variable>

using namespace Didrachma;

namespace {
using Bar = Market::Core::Series::Bar;
using Timestamp = Market::Core::Time::UtcTimestamp;

class FixtureAnalyzer final : public Analysis::Core::Indicator::Analyzer {
public:
    std::vector<Analysis::Core::Indicator::Definition> catalog() const override {
        return {};
    }
    Analysis::Core::Indicator::CalculationOutcome
    calculate(const Analysis::Core::Indicator::CalculationRequest&) override {
        return {};
    }
};

class FixtureProvider final : public Market::Core::Provider::Data {
public:
    std::vector<Bar> bars;

    Market::Core::Provider::CapabilitySet capabilities() const override {
        return Market::Core::Provider::CapabilitySet::from(Market::Core::Provider::Capability::History);
    }
    Market::Core::Provider::HistoryResult load_history(const Market::Core::Provider::HistoryRequest& value) override {
        std::vector<Bar> selected;
        for (const auto& item : bars)
            if (item.close_time && *item.close_time >= value.range.begin && *item.close_time < value.range.end)
                selected.push_back(item);
        return selected;
    }
    std::unique_ptr<Market::Core::Provider::Subscription> subscribe(const Market::Core::Series::Key&,
                                                                    Market::Core::Provider::UpdateHandler) override {
        return {};
    }
};

Timestamp at(std::int64_t seconds) {
    return Timestamp{std::chrono::seconds{seconds}};
}

Bar bar(std::int64_t close, double open, double high, double low, double value) {
    return {at(close - 600), at(close), open, high, low, value, 1000, Market::Core::Series::BarState::Closed};
}

Strategy::Core::BacktestRequest request() {
    Strategy::Core::Definition definition;
    definition.id = "immutable";
    definition.display_name = "Submitted snapshot";
    definition.description = "all request fields are retained";
    return {definition,
            std::optional<std::string>{"AAPL"},
            Market::Core::Time::UtcTimestamp{std::chrono::seconds{100}},
            Market::Core::Time::UtcTimestamp{std::chrono::seconds{200}},
            {"fixture", {{"configuration", "bars.json"}, {"variant", "review"}}},
            {3, 1000, 1, .02, .03},
            7};
}

Strategy::Core::BacktestRequest runner_request() {
    Strategy::Core::Definition definition;
    definition.id = "aapl-fixture";
    definition.display_name = "AAPL fixture";
    definition.primary_series_id = "primary";
    definition.series = {{"primary",
                          "fixture",
                          {Strategy::Core::InstrumentKind::Subject, {}},
                          {10, Market::Core::Time::Unit::Minute},
                          {},
                          "AAPL 10 minutes"}};
    auto entry = std::make_shared<Strategy::Core::ConditionExpression>();
    entry->id = "close-above-100";
    entry->kind = Strategy::Core::ConditionKind::MarketComparison;
    entry->predicate = Strategy::Core::MarketComparison{"primary", Strategy::Core::MarketField::Close,
                                                        Strategy::Core::Comparison::Greater, 100};
    definition.entry.condition = entry;
    definition.entry.order = {Strategy::Core::EntryOrderKind::NextBarOpen};
    definition.stop_loss = {Strategy::Core::PricePolicyKind::Absolute, 95};
    definition.target = {Strategy::Core::PricePolicyKind::Absolute, 110};
    return {definition, "AAPL", at(1200), at(3000), {"fixture", {}}, {2, 1000, 1, .5, 0}, 1};
}

Strategy::Core::BacktestOutcome completed(const Strategy::Core::BacktestRequest& value) {
    Strategy::Core::BacktestOutcome outcome;
    outcome.request = value;
    outcome.status = Strategy::Core::BacktestStatus::Completed;
    outcome.result_status = Strategy::Core::BacktestResultStatus::NoSignal;
    return outcome;
}

Strategy::Core::BacktestOutcome completed_with_projection(const Strategy::Core::BacktestRequest& value) {
    auto outcome = completed(value);
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
    outcome.result_status = Strategy::Core::BacktestResultStatus::Exited;
    return outcome;
}

void apply_until_finished(Studio::Core::HistoricalBacktests& runs) {
    while (!runs.runs().back().outcome) {
        runs.apply_pending();
        std::this_thread::yield();
    }
}

int test_exact_snapshot_progress_and_panel_independent_drain() {
    std::mutex mutex;
    std::condition_variable entered;
    std::condition_variable release;
    bool running{};
    bool finish{};
    std::size_t executions{};
    Strategy::Core::BacktestRequest received;
    Studio::Core::HistoricalBacktests runs{[&](const auto& value, const auto&, const auto& progress) {
        {
            std::scoped_lock lock{mutex};
            ++executions;
            received = value;
            running = true;
        }
        entered.notify_one();
        progress({Strategy::Core::BacktestProgressKind::Preparing});
        progress({Strategy::Core::BacktestProgressKind::Loading, "primary"});
        progress({Strategy::Core::BacktestProgressKind::Ready, "primary"});
        progress({Strategy::Core::BacktestProgressKind::Executing});
        std::unique_lock lock{mutex};
        release.wait(lock, [&] { return finish; });
        return completed(value);
    }};

    auto submitted = request();
    CPPTEST_ASSERT(!runs.enqueue(submitted).empty());
    submitted.strategy_snapshot.display_name = "edited later";
    CPPTEST_ASSERT(runs.enqueue(submitted).empty());
    {
        std::unique_lock lock{mutex};
        entered.wait(lock, [&] { return running; });
        CPPTEST_ASSERT(executions == 1);
        CPPTEST_ASSERT(Strategy::Core::serialize(received) == Strategy::Core::serialize(request()));
        finish = true;
    }
    release.notify_one();
    apply_until_finished(runs); // The frame boundary drains this without any panel draw call.
    const auto& run = runs.runs().front();
    CPPTEST_ASSERT(run.outcome && run.outcome->status == Strategy::Core::BacktestStatus::Completed);
    const std::vector expected{
        Strategy::Core::BacktestProgressKind::Preparing, Strategy::Core::BacktestProgressKind::Loading,
        Strategy::Core::BacktestProgressKind::Ready, Strategy::Core::BacktestProgressKind::Executing,
        Strategy::Core::BacktestProgressKind::Completed};
    std::vector<Strategy::Core::BacktestProgressKind> actual;
    for (const auto& progress : run.progress_history)
        actual.push_back(progress.kind);
    CPPTEST_ASSERT(actual == expected && !runs.busy());
    return 0;
}

int test_cancel_wins_before_completion_commit() {
    std::mutex mutex;
    std::condition_variable entered;
    std::condition_variable release;
    bool running{};
    bool finish{};
    Studio::Core::HistoricalBacktests runs{[&](const auto& value, const auto&, const auto& progress) {
        {
            std::scoped_lock lock{mutex};
            running = true;
        }
        entered.notify_one();
        progress({Strategy::Core::BacktestProgressKind::Executing});
        std::unique_lock lock{mutex};
        release.wait(lock, [&] { return finish; });
        return completed(value);
    }};
    CPPTEST_ASSERT(!runs.enqueue(request()).empty());
    {
        std::unique_lock lock{mutex};
        entered.wait(lock, [&] { return running; });
        runs.request_cancel();
        finish = true;
    }
    release.notify_one();
    runs.cancel();
    const auto& run = runs.runs().front();
    CPPTEST_ASSERT(run.outcome && run.outcome->status == Strategy::Core::BacktestStatus::Cancelled);
    CPPTEST_ASSERT(run.outcome->errors.size() == 1 &&
                   run.outcome->errors.front().code == Strategy::Core::BacktestErrorCode::Cancelled);
    CPPTEST_ASSERT(std::ranges::count(run.progress_history, Strategy::Core::BacktestProgressKind::Cancelled,
                                      &Strategy::Core::BacktestProgress::kind) == 1);
    CPPTEST_ASSERT(std::ranges::none_of(run.progress_history, [](const auto& value) {
        return value.kind == Strategy::Core::BacktestProgressKind::Completed;
    }));
    return 0;
}

int test_completion_commit_wins_before_cancel() {
    Studio::Core::HistoricalBacktests runs{[](const auto& value, const auto&, const auto& progress) {
        progress({Strategy::Core::BacktestProgressKind::Executing});
        progress({Strategy::Core::BacktestProgressKind::Completed});
        return completed(value);
    }};
    CPPTEST_ASSERT(!runs.enqueue(request()).empty());
    apply_until_finished(runs);
    runs.request_cancel();
    runs.apply_pending();
    const auto& run = runs.runs().front();
    CPPTEST_ASSERT(run.outcome->status == Strategy::Core::BacktestStatus::Completed);
    CPPTEST_ASSERT(std::ranges::count(run.progress_history, Strategy::Core::BacktestProgressKind::Completed,
                                      &Strategy::Core::BacktestProgress::kind) == 1);
    CPPTEST_ASSERT(std::ranges::none_of(run.progress_history, [](const auto& value) {
        return value.kind == Strategy::Core::BacktestProgressKind::Cancelled;
    }));
    return 0;
}

int test_worker_exceptions_become_structured_failure() {
    Studio::Core::HistoricalBacktests runs{
        [](const auto&, const auto&, const auto&) -> Strategy::Core::BacktestOutcome {
            throw std::runtime_error{"unexpected executor error"};
        }};
    CPPTEST_ASSERT(!runs.enqueue(request()).empty());
    apply_until_finished(runs);
    const auto& run = runs.runs().front();
    CPPTEST_ASSERT(run.outcome->status == Strategy::Core::BacktestStatus::Failed);
    CPPTEST_ASSERT(run.outcome->errors.size() == 1 &&
                   run.outcome->errors.front().code == Strategy::Core::BacktestErrorCode::EngineFailure &&
                   run.outcome->errors.front().path == "/worker" &&
                   run.outcome->errors.front().message.find("unexpected executor error") != std::string::npos);
    CPPTEST_ASSERT(run.progress_history.back().kind == Strategy::Core::BacktestProgressKind::Failed);
    return 0;
}

int test_destruction_cancels_and_joins() {
    std::mutex mutex;
    std::condition_variable entered;
    bool destruction_entered{};
    {
        Studio::Core::HistoricalBacktests pending{[&](const auto& value, const auto& cancelled, const auto&) {
            {
                std::scoped_lock lock{mutex};
                destruction_entered = true;
            }
            entered.notify_one();
            while (!cancelled())
                std::this_thread::yield();
            return completed(value);
        }};
        CPPTEST_ASSERT(!pending.enqueue(request()).empty());
        std::unique_lock lock{mutex};
        entered.wait(lock, [&] { return destruction_entered; });
    }
    return 0;
}

int test_truthful_result_presentation_and_projection() {
    const auto make_run = [](Strategy::Core::BacktestStatus status,
                             Strategy::Core::BacktestResultStatus result_status) {
        Studio::Core::HistoricalBacktest run{"stored", request(), {}, {}, Strategy::Core::BacktestOutcome{}};
        run.outcome->request = run.request;
        run.outcome->status = status;
        run.outcome->result_status = result_status;
        return run;
    };
    for (const auto status :
         {Strategy::Core::BacktestResultStatus::NoSignal, Strategy::Core::BacktestResultStatus::SignalNotFilled,
          Strategy::Core::BacktestResultStatus::Failed}) {
        const auto run =
            make_run(status == Strategy::Core::BacktestResultStatus::Failed ? Strategy::Core::BacktestStatus::Failed
                                                                            : Strategy::Core::BacktestStatus::Completed,
                     status);
        const auto view = Studio::Core::present(run);
        CPPTEST_ASSERT(view.entry_price == "—" && view.net_profit_loss == "0.00");
    }
    auto cancelled = make_run(Strategy::Core::BacktestStatus::Cancelled, Strategy::Core::BacktestResultStatus::Failed);
    CPPTEST_ASSERT(Studio::Core::present(cancelled).terminal_status == "Cancelled");

    auto traded = make_run(Strategy::Core::BacktestStatus::Completed, Strategy::Core::BacktestResultStatus::Exited);
    traded.outcome->timings = {std::chrono::milliseconds{1}, std::chrono::milliseconds{2}, std::chrono::milliseconds{3},
                               std::chrono::milliseconds{7}};
    traded.request.strategy_snapshot.primary_series_id = "primary";
    traded.outcome->request.strategy_snapshot.primary_series_id = "primary";
    Strategy::Core::RunResult result;
    result.entry_time = Market::Core::Time::UtcTimestamp{std::chrono::seconds{120}};
    result.exit_time = Market::Core::Time::UtcTimestamp{std::chrono::seconds{180}};
    result.entry_price = 10.0;
    result.exit_price = 12.0;
    result.exit_reason = Strategy::Core::ExitReason::Target;
    result.gross_profit_loss = 6.0;
    result.net_profit_loss = 5.0;
    result.return_percentage = .5;
    result.duration = std::chrono::seconds{60};
    result.projection.markers = {{Strategy::Core::ProjectionKind::EntryMarker, *result.entry_time, *result.entry_price},
                                 {Strategy::Core::ProjectionKind::ExitMarker, *result.exit_time, *result.exit_price}};
    result.projection.segments = {
        {Strategy::Core::ProjectionKind::StopPrice, *result.entry_time, *result.exit_time, 9.0},
        {Strategy::Core::ProjectionKind::TargetPrice, *result.entry_time, *result.exit_time, 12.0}};
    Strategy::Core::Evidence evidence;
    evidence.condition_id = "leaf";
    evidence.condition_path = "group/sequence/leaf";
    evidence.truth = Strategy::Core::Truth::True;
    evidence.source_time = *result.entry_time;
    evidence.binding_id = "primary";
    evidence.source_key = Market::Core::Series::Key{"fixture", "AAPL", {1, Market::Core::Time::Unit::Day}};
    evidence.source_timeframe = evidence.source_key->timeframe;
    evidence.sequence_step = 1;
    evidence.sequence_size = 2;
    result.evaluations.push_back({*result.entry_time, Strategy::Core::Truth::False, {evidence}});
    traded.outcome->occurrences.push_back({0, Strategy::Core::BacktestResultStatus::Exited, result});
    traded.outcome->evaluations.push_back({0, result.evaluations.front()});
    traded.outcome->summary = {1, 1, 1, 1, 0, 0, 6.0, 1.0, 5.0};
    traded.outcome->inputs.push_back({{"primary", *evidence.source_key}, {Strategy::Core::Readiness::Ready}, {}});

    const auto view = Studio::Core::present(traded);
    CPPTEST_ASSERT(view.entry_price == "—" && view.exit_price == "—" && view.exit_reason == "—" &&
                   view.gross_profit_loss == "6.00" && view.net_profit_loss == "5.00" && view.costs == "1.00");
    CPPTEST_ASSERT(view.data_preparation == "1.00 ms" && view.indicator_calculation == "2.00 ms" &&
                   view.strategy_execution == "3.00 ms" && view.total_runner == "7.00 ms");
    const auto events = Studio::Core::timeline(traded);
    CPPTEST_ASSERT(events.size() == 1 && events.front().condition_path == "group/sequence/leaf" &&
                   events.front().binding_id == "primary" && events.front().source_key == evidence.source_key &&
                   events.front().source_timeframe == evidence.source_timeframe &&
                   events.front().time == *result.entry_time && events.front().sequence_step == 1);
    Studio::Core::HistoricalChartIntegration charts;
    Studio::Core::Workspace workspace;
    auto selection = charts.select(workspace, traded, events.front());
    CPPTEST_ASSERT(selection.document && selection.created && selection.document->series() == *evidence.source_key &&
                   selection.overlay && selection.overlay->annotations.size() == 4 &&
                   selection.overlay->position_span && selection.overlay->position_span->begin == result.entry_time &&
                   selection.overlay->position_span->end == result.exit_time);
    const Market::Core::Time::Range loaded{at(0), at(400)};
    auto navigation = charts.navigate(*selection.document, {at(100), at(200)}, loaded, *result.entry_time);
    CPPTEST_ASSERT(navigation.applied && !navigation.missing_history &&
                   selection.document->selected_timestamp() == result.entry_time &&
                   selection.document->visible_range().begin == navigation.visible_range.begin &&
                   selection.document->visible_range().end == navigation.visible_range.end);
    selection.document->dispatch(StockChart::Core::NavigateViewport{
        {navigation.visible_range.begin + std::chrono::seconds{10}, navigation.visible_range.end}});
    CPPTEST_ASSERT(selection.document->visible_range().begin ==
                   navigation.visible_range.begin + std::chrono::seconds{10});
    CPPTEST_ASSERT(charts.overlays_for(selection.document->id()).size() == 1);
    CPPTEST_ASSERT(charts.set_enabled(selection.document->id(), traded.id, 0, *evidence.source_key, false) &&
                   charts.overlays_for(selection.document->id()).empty());
    CPPTEST_ASSERT(charts.set_enabled(selection.document->id(), traded.id, 0, *evidence.source_key, true) &&
                   charts.overlays_for(selection.document->id()).size() == 1);
    auto pending = charts.navigate(*selection.document, navigation.visible_range, loaded, at(800));
    CPPTEST_ASSERT(!pending.applied && pending.missing_history &&
                   charts.overlays_for(selection.document->id()).size() == 1);
    auto completed_goto = charts.history_loaded(*selection.document, navigation.visible_range, {at(0), at(1000)});
    CPPTEST_ASSERT(completed_goto.applied && selection.document->selected_timestamp() == std::optional{at(800)} &&
                   charts.overlays_for(selection.document->id()).size() == 1);
    auto reused = charts.select(workspace, traded, events.front());
    CPPTEST_ASSERT(reused.document == selection.document && !reused.created && workspace.documents().size() == 1);
    const auto indicator = reused.document->add_indicator("manual-indicator", {});
    CPPTEST_ASSERT(charts.detach(reused.document->id()) && reused.document->find_indicator(indicator));
    const auto closed_id = reused.document->id();
    CPPTEST_ASSERT(workspace.close_chart(closed_id));
    auto reopened = charts.select(workspace, traded, events.front());
    CPPTEST_ASSERT(reopened.created && reopened.overlay && reopened.overlay->annotations.size() == 4);
    const Market::Core::Series::Key peer{"fixture", "SPY", {1, Market::Core::Time::Unit::Hour}};
    traded.outcome->inputs.push_back({{"peer", peer}, {Strategy::Core::Readiness::Ready}, {}});
    auto peer_event = events.front();
    peer_event.binding_id = "peer";
    peer_event.source_key = peer;
    peer_event.source_timeframe = peer.timeframe;
    auto peer_chart = charts.select(workspace, traded, peer_event);
    CPPTEST_ASSERT(peer_chart.document && peer_chart.document != selection.document &&
                   peer_chart.document->series() == peer && peer_chart.overlay &&
                   peer_chart.overlay->annotations.empty() && workspace.documents().size() == 2);

    StockChart::Core::Document manual{"manual", *evidence.source_key, {traded.request.from, traded.request.through}};
    manual.add_indicator("manual-indicator", {});
    const auto manual_indicators = manual.indicators().size();
    (void)charts.select(workspace, traded, events.front());
    CPPTEST_ASSERT(manual.id() == "manual" && manual.indicators().size() == manual_indicators);
    return 0;
}

int test_runner_fixture_flows_through_timeline_and_chart_projection() {
    FixtureAnalyzer analyzer;
    FixtureProvider provider;
    provider.bars = {bar(600, 98, 100, 97, 99), bar(1200, 99, 103, 98, 101), bar(1800, 102, 106, 101, 104),
                     bar(2400, 104, 111, 103, 110), bar(3000, 108, 111, 107, 110)};
    constexpr std::array supported{Market::Core::Time::Frame{10, Market::Core::Time::Unit::Minute}};
    const auto value = runner_request();
    auto outcome = Strategy::Core::BacktestRunner{analyzer}.run(value, provider, supported);
    CPPTEST_ASSERT(outcome.status == Strategy::Core::BacktestStatus::Completed && !outcome.occurrences.empty() &&
                   outcome.result_status == Strategy::Core::BacktestResultStatus::Exited);

    Studio::Core::HistoricalBacktest stored{"fixture-run", value, {}, {}, std::move(outcome)};
    const auto view = Studio::Core::present(stored);
    CPPTEST_ASSERT(view.inclusive_range == "1970-01-01 00:20:00 UTC — 1970-01-01 00:50:00 UTC");
    const auto items = Studio::Core::timeline(stored);
    CPPTEST_ASSERT(!items.empty());
    const auto condition = std::ranges::find_if(items, [](const auto& item) {
        return item.kind == Studio::Core::HistoricalTimelineKind::Evaluation &&
               item.truth == Strategy::Core::Truth::True;
    });
    const auto audit = std::ranges::find(items, Studio::Core::HistoricalTimelineKind::AuditEvent,
                                         &Studio::Core::HistoricalTimelineItem::kind);
    CPPTEST_ASSERT(condition != items.end());
    CPPTEST_ASSERT(condition->condition_path.find("close-above-100") != std::string::npos);
    CPPTEST_ASSERT(condition->truth == Strategy::Core::Truth::True && condition->measured_value);
    CPPTEST_ASSERT(!condition->detail.empty() && condition->source_key);
    CPPTEST_ASSERT(condition->source_key->instrument == "AAPL" && condition->source_timeframe == supported.front());
    CPPTEST_ASSERT(audit != items.end() && audit->result_kind == "Entry armed" && audit->source_key &&
                   audit->source_timeframe == supported.front());
    CPPTEST_ASSERT(Studio::Core::format_timeframe(*condition->source_timeframe) == "10 minutes" &&
                   Studio::Core::format_truth(condition->truth) == "True");

    Studio::Core::HistoricalChartIntegration charts;
    Studio::Core::Workspace workspace;
    const auto condition_chart = charts.select(workspace, stored, *condition);
    const auto audit_chart = charts.select(workspace, stored, *audit);
    CPPTEST_ASSERT(condition_chart.document && audit_chart.document == condition_chart.document &&
                   !condition_chart.retained_bars.empty() && condition_chart.overlay &&
                   condition_chart.overlay->annotations.size() >= 4);
    const auto* history = workspace.history_request(condition_chart.document->id());
    CPPTEST_ASSERT(history && history->range.begin == stored.outcome->inputs.front().bars.front().open_time &&
                   stored.outcome->inputs.front().bars.back().close_time &&
                   history->range.end == *stored.outcome->inputs.front().bars.back().close_time);
    CPPTEST_ASSERT(!stored.outcome->occurrences.empty() &&
                   !stored.outcome->occurrences.front().result.evaluations.empty());
    return 0;
}

} // namespace

int main() {
    CPPTEST_RUN(test_exact_snapshot_progress_and_panel_independent_drain);
    CPPTEST_RUN(test_cancel_wins_before_completion_commit);
    CPPTEST_RUN(test_completion_commit_wins_before_cancel);
    CPPTEST_RUN(test_worker_exceptions_become_structured_failure);
    CPPTEST_RUN(test_destruction_cancels_and_joins);
    CPPTEST_RUN(test_truthful_result_presentation_and_projection);
    CPPTEST_RUN(test_runner_fixture_flows_through_timeline_and_chart_projection);
    return 0;
}
