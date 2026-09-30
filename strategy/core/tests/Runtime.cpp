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
    value.series = {{"primary",
                     "fixture",
                     {InstrumentKind::Fixed, "TEST"},
                     {1, Market::Core::Time::Unit::Minute},
                     {},
                     "Primary test series"}};
    value.entry.condition = close_condition("entry", Comparison::Greater, 100);
    value.entry.order = {EntryOrderKind::NextBarOpen};
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

int complete_equivalence(const RunResult& left, const RunResult& right) {
    CPPTEST_ASSERT(left.state == right.state && left.entry_status == right.entry_status &&
                   left.exit_reason == right.exit_reason);
    CPPTEST_ASSERT(left.entry_time == right.entry_time && left.exit_time == right.exit_time &&
                   left.entry_price == right.entry_price && left.exit_price == right.exit_price &&
                   left.current_price == right.current_price);
    CPPTEST_ASSERT(left.unrealized_profit_loss == right.unrealized_profit_loss &&
                   left.unrealized_return_percentage == right.unrealized_return_percentage &&
                   left.gross_profit_loss == right.gross_profit_loss && left.net_profit_loss == right.net_profit_loss &&
                   left.return_percentage == right.return_percentage);
    CPPTEST_ASSERT(left.duration == right.duration && left.elapsed == right.elapsed &&
                   left.maximum_favorable_excursion == right.maximum_favorable_excursion &&
                   left.maximum_adverse_excursion == right.maximum_adverse_excursion &&
                   left.closed_bar_count == right.closed_bar_count && left.trigger_counts == right.trigger_counts &&
                   left.ambiguous_fill == right.ambiguous_fill && left.warnings == right.warnings);
    CPPTEST_ASSERT(left.events.size() == right.events.size());
    for (std::size_t index = 0; index < left.events.size(); ++index) {
        const auto& a = left.events[index];
        const auto& b = right.events[index];
        CPPTEST_ASSERT(a.kind == b.kind && a.evaluation_time == b.evaluation_time &&
                       a.effective_time == b.effective_time && a.old_state == b.old_state &&
                       a.new_state == b.new_state && a.trigger_ids == b.trigger_ids && a.old_value == b.old_value &&
                       a.new_value == b.new_value && a.detail == b.detail && a.evidence.size() == b.evidence.size());
        for (std::size_t evidence = 0; evidence < a.evidence.size(); ++evidence) {
            const auto& x = a.evidence[evidence];
            const auto& y = b.evidence[evidence];
            CPPTEST_ASSERT(x.condition_id == y.condition_id && x.truth == y.truth && x.value == y.value &&
                           x.source_time == y.source_time && x.detail == y.detail);
        }
    }
    CPPTEST_ASSERT(left.projection.markers.size() == right.projection.markers.size() &&
                   left.projection.segments.size() == right.projection.segments.size());
    for (std::size_t index = 0; index < left.projection.markers.size(); ++index) {
        const auto& a = left.projection.markers[index];
        const auto& b = right.projection.markers[index];
        CPPTEST_ASSERT(a.kind == b.kind && a.time == b.time && a.price == b.price);
    }
    for (std::size_t index = 0; index < left.projection.segments.size(); ++index) {
        const auto& a = left.projection.segments[index];
        const auto& b = right.projection.segments[index];
        CPPTEST_ASSERT(a.kind == b.kind && a.begin == b.begin && a.end == b.end && a.price == b.price);
    }
    return 0;
}
} // namespace

int test_next_open_and_final_bar_signal_status() {
    auto model = definition();
    model.target = {PricePolicyKind::Absolute, 150};
    auto result = run(model, {bar(0, 99, 102, 98, 101), bar(60, 103, 105, 102, 104)});
    CPPTEST_ASSERT(result.entry_status == EntryStatus::Filled && near(*result.entry_price, 103));

    result = run(model, {bar(0, 99, 102, 98, 101)});
    CPPTEST_ASSERT(result.state == RunState::EntryArmed && result.entry_status == EntryStatus::AwaitingFill);
    CPPTEST_ASSERT(result.events.size() == 1 && result.events.front().kind == StrategyEventKind::EntryArmed);

    result = run(model, {bar(0, 98, 99, 97, 98)});
    CPPTEST_ASSERT(result.state == RunState::WaitingForEntry && result.entry_status == EntryStatus::NoSignal);
    return 0;
}

int test_long_and_short_limit_touch_and_favorable_gap_fills() {
    auto long_model = definition();
    long_model.entry.order = {EntryOrderKind::Limit, 100, 3};
    long_model.target = {PricePolicyKind::Absolute, 150};
    auto result = run(long_model, {bar(0, 99, 102, 98, 101), bar(60, 105, 106, 99, 102)});
    CPPTEST_ASSERT(result.entry_status == EntryStatus::Filled && near(*result.entry_price, 100));
    CPPTEST_ASSERT(result.events[1].detail == "limit entry price touched");

    result = run(long_model, {bar(0, 99, 102, 98, 101), bar(60, 95, 101, 94, 99)});
    CPPTEST_ASSERT(near(*result.entry_price, 95));
    CPPTEST_ASSERT(result.events[1].detail == "limit entry filled at favorable bar open");

    auto short_model = definition(Direction::Short);
    short_model.entry.condition = close_condition("entry", Comparison::Less, 100);
    short_model.entry.order = {EntryOrderKind::Limit, 100, 3};
    short_model.target = {PricePolicyKind::Absolute, 50};
    result = run(short_model, {bar(0, 101, 102, 98, 99), bar(60, 95, 101, 94, 98)});
    CPPTEST_ASSERT(near(*result.entry_price, 100));
    result = run(short_model, {bar(0, 101, 102, 98, 99), bar(60, 105, 106, 99, 103)});
    CPPTEST_ASSERT(near(*result.entry_price, 105));
    return 0;
}

int test_limit_expiry_and_percentage_policies_use_actual_fill() {
    auto model = definition();
    model.entry.order = {EntryOrderKind::Limit, 90, 2};
    model.target = {PricePolicyKind::Absolute, 150};
    auto result = run(model, {bar(0, 99, 102, 98, 101), bar(60, 105, 106, 100, 104), bar(120, 104, 105, 99, 103)});
    CPPTEST_ASSERT(result.state == RunState::WaitingForEntry && result.entry_status == EntryStatus::Expired);
    CPPTEST_ASSERT(result.events.back().kind == StrategyEventKind::EntryExpired);
    CPPTEST_ASSERT(result.events.back().effective_time == at(180));

    model.entry.order = {EntryOrderKind::Limit, 100, 2};
    model.stop_loss = {PricePolicyKind::PercentageFromEntry, 10};
    model.target = {PricePolicyKind::PercentageFromEntry, 10};
    result = run(model, {bar(0, 99, 102, 98, 101), bar(60, 95, 100, 94, 98)}, {1, {}, 0, 0, 1});
    CPPTEST_ASSERT(near(*result.entry_price, 95.95));
    CPPTEST_ASSERT(near(result.projection.segments[1].price, 86.355));
    CPPTEST_ASSERT(near(result.projection.segments[2].price, 105.545));
    return 0;
}

int test_limit_same_bar_ambiguity_is_conservative() {
    auto model = definition();
    model.entry.order = {EntryOrderKind::Limit, 100, 2};
    const auto result = run(model, {bar(0, 99, 102, 98, 101), bar(60, 105, 112, 89, 101)});
    CPPTEST_ASSERT(result.exit_reason == ExitReason::StopLoss && result.ambiguous_fill);
    CPPTEST_ASSERT(result.warnings.front().find("limit entry") != std::string::npos);
    return 0;
}

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
    model.entry.order = {EntryOrderKind::Limit, 100, 2};
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
    CPPTEST_ASSERT(complete_equivalence(replayed, live) == 0);

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

int test_forming_updates_do_not_rewrite_committed_decisions() {
    auto model = definition();
    model.target = {PricePolicyKind::Absolute, 150};
    const std::vector bars{bar(0, 99, 102, 98, 101), bar(60, 100, 104, 99, 103), bar(120, 104, 106, 102, 105)};
    Analyzer analyzer;
    DataGraph graph(Snapshot{model}, analyzer, {});
    const auto key = graph.resolution().series.front().key;
    Engine engine(Snapshot{model}, graph);
    for (const auto& closed : bars) {
        auto forming = closed;
        forming.state = Market::Core::Series::BarState::Forming;
        forming.close_time.reset();
        graph.apply({key, Market::Core::Series::BarUpdateKind::Backfill, {forming}});
        engine.process(forming);
        const auto before = engine.result();

        forming.close += 25;
        forming.high += 25;
        graph.apply({key, Market::Core::Series::BarUpdateKind::ReplaceForming, {forming}});
        engine.process(forming);
        CPPTEST_ASSERT(complete_equivalence(before, engine.result()) == 0);

        graph.apply({key, Market::Core::Series::BarUpdateKind::ReplaceForming, {closed}});
        engine.process(closed);
    }

    DataGraph replay_graph(Snapshot{model}, analyzer, {});
    replay_graph.set_series("primary", bars);
    Engine replay_engine(Snapshot{model}, replay_graph);
    CPPTEST_ASSERT(complete_equivalence(replay_engine.replay(), engine.finish()) == 0);
    return 0;
}

int test_explicit_historical_corrections_are_auditable_and_require_a_new_replay() {
    auto model = definition();
    model.target = {PricePolicyKind::Absolute, 110};
    const std::vector original{bar(0, 99, 102, 98, 101), bar(60, 100, 108, 99, 107), bar(120, 107, 109, 105, 108)};
    Analyzer analyzer;
    DataGraph graph(Snapshot{model}, analyzer, {});
    const auto key = graph.resolution().series.front().key;
    graph.apply({key, Market::Core::Series::BarUpdateKind::Reset, original});
    Engine committed_engine(Snapshot{model}, graph);
    const auto committed = committed_engine.replay();
    const auto committed_copy = committed;

    auto forming = original.back();
    forming.state = Market::Core::Series::BarState::Forming;
    forming.close_time.reset();
    forming.close = 150;
    const auto audit_before_forming = graph.input_revision_audit().size();
    graph.apply({key, Market::Core::Series::BarUpdateKind::ReplaceForming, {forming}});
    CPPTEST_ASSERT(graph.input_revision_audit().size() == audit_before_forming);
    CPPTEST_ASSERT(complete_equivalence(committed_copy, committed) == 0);

    auto corrected = original[1];
    corrected.high = 112;
    corrected.close = 111;
    graph.apply({key, Market::Core::Series::BarUpdateKind::Backfill, {corrected}});
    const auto backfill = graph.input_revision_audit().back();
    CPPTEST_ASSERT(backfill.binding_id == "primary" && backfill.key == key && backfill.revision == 2 &&
                   backfill.update_kind == Market::Core::Series::BarUpdateKind::Backfill &&
                   backfill.affected_range.begin == corrected.open_time &&
                   backfill.affected_range.end == *corrected.close_time);
    Engine corrected_engine(Snapshot{model}, graph);
    const auto corrected_run = corrected_engine.replay();
    CPPTEST_ASSERT(corrected_run.exit_reason == ExitReason::Target && committed.exit_reason == ExitReason::EndOfRange);
    CPPTEST_ASSERT(corrected_run.exit_price != committed.exit_price);
    CPPTEST_ASSERT(complete_equivalence(corrected_run, corrected_engine.result()) == 0);
    CPPTEST_ASSERT(complete_equivalence(committed_copy, committed) == 0);

    graph.apply({key, Market::Core::Series::BarUpdateKind::Reset, original});
    const auto reset = graph.input_revision_audit().back();
    CPPTEST_ASSERT(reset.binding_id == "primary" && reset.revision == 3 &&
                   reset.update_kind == Market::Core::Series::BarUpdateKind::Reset &&
                   reset.affected_range.begin == original.front().open_time &&
                   reset.affected_range.end == *original.back().close_time);
    return 0;
}

int main() {
    CPPTEST_RUN(test_next_open_and_final_bar_signal_status);
    CPPTEST_RUN(test_long_and_short_limit_touch_and_favorable_gap_fills);
    CPPTEST_RUN(test_limit_expiry_and_percentage_policies_use_actual_fill);
    CPPTEST_RUN(test_limit_same_bar_ambiguity_is_conservative);
    CPPTEST_RUN(test_target_stop_gap_and_ambiguous_fill_rules);
    CPPTEST_RUN(test_condition_exit_and_end_of_range_close);
    CPPTEST_RUN(test_dynamic_target_and_stop_are_historical_segments);
    CPPTEST_RUN(test_long_short_costs_slippage_roi_duration_and_audit);
    CPPTEST_RUN(test_runtime_exit_user_stop_restart_and_live_replay_equivalence);
    CPPTEST_RUN(test_forming_updates_do_not_rewrite_committed_decisions);
    CPPTEST_RUN(test_explicit_historical_corrections_are_auditable_and_require_a_new_replay);
    return 0;
}
