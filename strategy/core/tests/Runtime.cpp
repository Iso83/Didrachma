#include "TestAssert.h"

#include <Didrachma/strategy/core/Runtime.h>
#include <cmath>

using namespace Didrachma;
using namespace Didrachma::Strategy::Core;
using namespace std::chrono_literals;

namespace {
using Bar = Market::Core::Series::Bar;
using Timestamp = Market::Core::Time::UtcTimestamp;

class Analyzer final : public Analysis::Core::Indicator::Analyzer {
public:
    std::vector<Analysis::Core::Indicator::Definition> catalog() const override {
        return {};
    }
    Analysis::Core::Indicator::CalculationOutcome
    calculate(const Analysis::Core::Indicator::CalculationRequest& request) override {
        Analysis::Core::Indicator::Result result;
        result.state = Analysis::Core::Indicator::CalculationState::Ready;
        result.outputs.push_back({"value"});
        for (const auto& bar : request.bars)
            result.outputs.front().samples.push_back({bar.open_time, bar.close});
        return {std::move(result), Analysis::Core::Indicator::RecalculationKind::Full};
    }
};

Timestamp at(std::int64_t seconds) {
    return Timestamp{std::chrono::seconds{seconds}};
}

Bar bar(std::int64_t open_time, double open, double high, double low, double close) {
    return {at(open_time), at(open_time + 60), open, high, low, close, 1000, Market::Core::Series::BarState::Closed};
}

std::shared_ptr<ConditionExpression> close_condition(std::string id, Comparison comparison, double value) {
    auto condition = std::make_shared<ConditionExpression>();
    condition->id = std::move(id);
    condition->kind = ConditionKind::MarketComparison;
    condition->predicate = MarketComparison{"primary", MarketField::Close, comparison, value};
    return condition;
}

Definition definition(Direction direction = Direction::Long) {
    Definition value;
    value.id = "runtime";
    value.display_name = "Runtime fixture";
    value.direction = direction;
    value.primary_series_id = "primary";
    value.series = {{"primary", "fixture", {InstrumentKind::Fixed, "TEST"}, {1, Market::Core::Time::Unit::Minute}, {}}};
    value.entry.condition = close_condition("entry", Comparison::Greater, 100);
    value.entry.price = {PricePolicyKind::Absolute, 0};
    value.stop_loss = {PricePolicyKind::Absolute, direction == Direction::Long ? 90.0 : 110.0};
    value.target = {PricePolicyKind::Absolute, direction == Direction::Long ? 110.0 : 90.0};
    return value;
}

RunResult run(Definition model, std::vector<Bar> bars, ExecutionCosts costs = {}) {
    Analyzer analyzer;
    DataGraph graph(Snapshot{model}, analyzer, {});
    graph.set_series("primary", bars);
    Engine engine(Snapshot{model}, graph, costs);
    return engine.replay();
}

bool near(double left, double right) {
    return std::abs(left - right) < 0.000001;
}
} // namespace

int test_target_stop_gap_and_ambiguous_fill_rules() {
    const std::vector target_bars{bar(0, 99, 102, 98, 101), bar(60, 100, 111, 99, 108)};
    auto result = run(definition(), target_bars);
    CPPTEST_ASSERT(result.exit_reason == ExitReason::Target && near(*result.exit_price, 110));
    CPPTEST_ASSERT(result.events[0].kind == StrategyEventKind::EntryArmed);
    CPPTEST_ASSERT(result.events[1].kind == StrategyEventKind::EntryFilled);

    const std::vector stop_bars{bar(0, 99, 102, 98, 101), bar(60, 100, 105, 89, 92)};
    result = run(definition(), stop_bars);
    CPPTEST_ASSERT(result.exit_reason == ExitReason::StopLoss && near(*result.exit_price, 90));

    const std::vector gap_bars{bar(0, 99, 102, 98, 101), bar(60, 85, 88, 80, 82)};
    result = run(definition(), gap_bars);
    CPPTEST_ASSERT(result.exit_reason == ExitReason::StopLoss && near(*result.exit_price, 85));

    const std::vector ambiguous_bars{bar(0, 99, 102, 98, 101), bar(60, 100, 112, 88, 101)};
    result = run(definition(), ambiguous_bars);
    CPPTEST_ASSERT(result.exit_reason == ExitReason::StopLoss && result.ambiguous_fill);
    CPPTEST_ASSERT(result.warnings.size() == 1);
    return 0;
}

int test_condition_exit_and_end_of_range_close() {
    auto model = definition();
    model.target = {PricePolicyKind::Absolute, 150};
    model.exits = {{"weakness", close_condition("weak-close", Comparison::Less, 98)}};
    const std::vector bars{bar(0, 99, 102, 98, 101), bar(60, 100, 104, 95, 97), bar(120, 96, 99, 94, 98)};
    auto result = run(model, bars);
    CPPTEST_ASSERT(result.exit_reason == ExitReason::Condition && near(*result.exit_price, 96));
    CPPTEST_ASSERT(result.events[result.events.size() - 2].kind == StrategyEventKind::ExitArmed);
    CPPTEST_ASSERT(result.events.back().effective_time == at(120));

    model.exits.clear();
    result = run(model, {bar(0, 99, 102, 98, 101), bar(60, 100, 104, 95, 103)});
    CPPTEST_ASSERT(result.exit_reason == ExitReason::EndOfRange && near(*result.exit_price, 103));
    return 0;
}

int test_dynamic_target_and_stop_are_historical_segments() {
    auto target_model = definition();
    target_model.target = {PricePolicyKind::Absolute, 130};
    target_model.runtime_rules = {{"lower-target",
                                   10,
                                   close_condition("adverse-volatility", Comparison::Less, 99),
                                   {{ActionKind::AdjustTarget, PricePolicy{PricePolicyKind::Absolute, 104}, ""}}}};
    auto target_result =
        run(target_model, {bar(0, 99, 102, 98, 101), bar(60, 100, 103, 95, 98), bar(120, 99, 105, 97, 104)});
    CPPTEST_ASSERT(target_result.exit_reason == ExitReason::Target && near(*target_result.exit_price, 104));
    CPPTEST_ASSERT(target_result.events[2].kind == StrategyEventKind::TargetAdjusted);
    CPPTEST_ASSERT(target_result.projection.segments.size() == 4);

    auto stop_model = definition();
    stop_model.target = {PricePolicyKind::Absolute, 140};
    stop_model.runtime_rules = {{"tighten-stop",
                                 10,
                                 close_condition("favorable-progress", Comparison::Greater, 108),
                                 {{ActionKind::AdjustStop, PricePolicy{PricePolicyKind::Absolute, 105}, ""}}}};
    auto stop_result =
        run(stop_model, {bar(0, 99, 102, 98, 101), bar(60, 100, 111, 99, 109), bar(120, 108, 109, 104, 106)});
    CPPTEST_ASSERT(stop_result.exit_reason == ExitReason::StopLoss && near(*stop_result.exit_price, 105));
    CPPTEST_ASSERT(stop_result.events[2].kind == StrategyEventKind::StopAdjusted);
    CPPTEST_ASSERT(stop_result.projection.segments.back().price == 105);
    return 0;
}

int test_long_short_costs_slippage_roi_duration_and_audit() {
    ExecutionCosts costs{2, 1000, 1, 0.5, 1};
    auto long_result = run(definition(), {bar(0, 99, 102, 98, 101), bar(60, 100, 112, 99, 111)}, costs);
    CPPTEST_ASSERT(near(*long_result.entry_price, 101) && near(*long_result.exit_price, 108.9));
    CPPTEST_ASSERT(near(long_result.gross_profit_loss, 15.8));
    CPPTEST_ASSERT(near(long_result.net_profit_loss, 11.701));
    CPPTEST_ASSERT(near(long_result.return_percentage, 1.1701));
    CPPTEST_ASSERT(long_result.duration == 0s);
    CPPTEST_ASSERT(!long_result.events.back().trigger_ids.empty());

    auto short_model = definition(Direction::Short);
    short_model.entry.condition = close_condition("entry", Comparison::Less, 100);
    const auto short_result = run(short_model, {bar(0, 101, 102, 98, 99), bar(60, 100, 101, 89, 91)});
    CPPTEST_ASSERT(short_result.exit_reason == ExitReason::Target);
    CPPTEST_ASSERT(near(short_result.gross_profit_loss, 10));
    CPPTEST_ASSERT(short_result.maximum_favorable_excursion >= 0);
    return 0;
}

int test_runtime_exit_user_stop_restart_and_live_replay_equivalence() {
    auto model = definition();
    model.target = {PricePolicyKind::Absolute, 150};
    auto elapsed = std::make_shared<ConditionExpression>();
    elapsed->id = "elapsed";
    elapsed->kind = ConditionKind::ElapsedTime;
    elapsed->predicate = ElapsedTimeCondition{Comparison::GreaterOrEqual, 60s};
    model.runtime_rules = {{"timed", 1, elapsed, {{ActionKind::Exit, {}, "time limit"}}}};
    const std::vector bars{bar(0, 99, 102, 98, 101), bar(60, 100, 104, 99, 103), bar(120, 104, 106, 102, 105),
                           bar(180, 106, 108, 104, 107)};

    Analyzer analyzer;
    DataGraph graph(Snapshot{model}, analyzer, {});
    graph.set_series("primary", bars);
    Engine replay_engine(Snapshot{model}, graph);
    const auto replayed = replay_engine.replay();
    DataGraph live_graph(Snapshot{model}, analyzer, {});
    const auto live_key = live_graph.resolution().series.front().key;
    Engine live_engine(Snapshot{model}, live_graph);
    for (const auto& item : bars) {
        live_graph.apply({live_key, Market::Core::Series::BarUpdateKind::AppendClosed, {item}});
        live_engine.process(item);
    }
    const auto live = live_engine.finish();
    CPPTEST_ASSERT(replayed.exit_reason == ExitReason::RuntimeRule);
    CPPTEST_ASSERT(replayed.events.size() == live.events.size());
    CPPTEST_ASSERT(replayed.net_profit_loss == live.net_profit_loss);

    auto mutable_model = definition();
    DataGraph isolated_graph(Snapshot{mutable_model}, analyzer, {});
    isolated_graph.set_series("primary", bars);
    Engine isolated(Snapshot{mutable_model}, isolated_graph);
    mutable_model.target.value = 101;
    CPPTEST_ASSERT(isolated.replay().exit_reason == ExitReason::EndOfRange);
    CPPTEST_ASSERT(near(*isolated.result().exit_price, 107));

    DataGraph stopped_graph(Snapshot{model}, analyzer, {});
    stopped_graph.set_series("primary", bars);
    Engine stopped(Snapshot{model}, stopped_graph);
    stopped.process(bars[0]);
    stopped.stop(at(60));
    CPPTEST_ASSERT(stopped.result().state == RunState::Stopped);
    CPPTEST_ASSERT(stopped.result().exit_reason == ExitReason::UserStop);
    return 0;
}

int main() {
    CPPTEST_RUN(test_target_stop_gap_and_ambiguous_fill_rules);
    CPPTEST_RUN(test_condition_exit_and_end_of_range_close);
    CPPTEST_RUN(test_dynamic_target_and_stop_are_historical_segments);
    CPPTEST_RUN(test_long_short_costs_slippage_roi_duration_and_audit);
    CPPTEST_RUN(test_runtime_exit_user_stop_restart_and_live_replay_equivalence);
    return 0;
}
