#include <Didrachma/strategy/core/Runtime.h>
#include <algorithm>
#include <cmath>

namespace Didrachma::Strategy::Core::Intern {
using Bar = Market::Core::Series::Bar;
using Timestamp = Market::Core::Time::UtcTimestamp;

bool compare_time(const Bar& left, const Bar& right) {
    return left.open_time < right.open_time;
}
} // namespace Didrachma::Strategy::Core::Intern

namespace Didrachma::Strategy::Core {
struct Engine::Implementation {
    Snapshot snapshot;
    DataGraph* graph;
    ExecutionCosts costs;
    RunResult result;
    std::optional<double> stop_price;
    std::optional<double> target_price;
    std::optional<Intern::Timestamp> last_time;
    std::optional<Intern::Timestamp> armed_time;
    ExitReason pending_exit{ExitReason::None};
    std::vector<std::string> pending_ids;
    std::vector<Evidence> pending_evidence;

    Implementation(Snapshot value, DataGraph& data, ExecutionCosts execution)
        : snapshot(std::move(value)), graph(&data), costs(execution) {
        if (costs.quantity <= 0 || costs.fixed_per_fill < 0 || costs.percentage_per_fill < 0 ||
            costs.slippage_percentage < 0)
            fail({}, "execution costs and quantity must be non-negative, with positive quantity");
    }

    const Definition& definition() const {
        return snapshot.definition();
    }

    void event(StrategyEventKind kind, Intern::Timestamp evaluated, Intern::Timestamp effective, RunState old_state,
               RunState new_state, std::vector<std::string> ids = {}, std::optional<double> old_value = {},
               std::optional<double> new_value = {}, std::vector<Evidence> evidence = {}, std::string detail = {}) {
        for (const auto& id : ids)
            ++result.trigger_counts[id];
        result.events.push_back({kind, evaluated, effective, old_state, new_state, std::move(ids), old_value, new_value,
                                 std::move(evidence), std::move(detail)});
        result.state = new_state;
    }

    void fail(Intern::Timestamp time, std::string detail) {
        event(StrategyEventKind::Error, time, time, result.state, RunState::Error, {}, {}, {}, {}, detail);
        result.exit_reason = ExitReason::Error;
    }

    double slipped(double price, bool buying) const {
        const auto factor = costs.slippage_percentage / 100.0;
        return price * (buying ? 1.0 + factor : 1.0 - factor);
    }

    std::optional<double> policy(const PricePolicy& value, Intern::Timestamp time, bool favorable) const {
        if (value.kind == PricePolicyKind::Absolute)
            return value.value;

        if (value.kind == PricePolicyKind::IndicatorValue) {
            const auto indicator = graph->indicator_value(value.indicator_id, value.output_id, time);
            return indicator ? std::optional{*indicator + value.offset} : std::nullopt;
        }

        if (!result.entry_price)
            return std::nullopt;

        const auto direction = definition().direction == Direction::Long ? 1.0 : -1.0;
        const auto sign = favorable ? direction : -direction;
        return *result.entry_price * (1.0 + sign * value.value / 100.0);
    }

    RunValues values(Intern::Timestamp time, double current) const {
        const auto direction = definition().direction == Direction::Long ? 1.0 : -1.0;
        return {result.entry_time ? std::chrono::duration_cast<Duration>(time - *result.entry_time) : Duration{},
                result.closed_bar_count,
                result.entry_price ? direction * (current - *result.entry_price) / *result.entry_price * 100.0 : 0.0};
    }

    void open_segments(Intern::Timestamp time) {
        result.projection.segments.push_back({ProjectionKind::EntryPrice, time, time, *result.entry_price});
        result.projection.segments.push_back({ProjectionKind::StopPrice, time, time, *stop_price});
        result.projection.segments.push_back({ProjectionKind::TargetPrice, time, time, *target_price});
    }

    void extend_segments(Intern::Timestamp time) {
        for (const auto kind : {ProjectionKind::EntryPrice, ProjectionKind::StopPrice, ProjectionKind::TargetPrice}) {
            const auto found = std::ranges::find(result.projection.segments.rbegin(), result.projection.segments.rend(),
                                                 kind, &PriceSegment::kind);
            if (found != result.projection.segments.rend() && found->end < time)
                found->end = time;
        }
    }

    void adjust_segment(ProjectionKind kind, Intern::Timestamp time, double price) {
        extend_segments(time);
        result.projection.segments.push_back({kind, time, time, price});
    }

    void enter(const Intern::Bar& bar) {
        const auto buying = definition().direction == Direction::Long;
        result.entry_time = bar.open_time;
        result.entry_price = slipped(bar.open, buying);
        result.current_price = *result.entry_price;
        stop_price = policy(definition().stop_loss, bar.open_time, false);
        target_price = policy(definition().target, bar.open_time, true);
        if (!stop_price || !target_price) {
            fail(bar.open_time, "entry stop or target price is unavailable");
            return;
        }

        result.projection.markers.push_back({ProjectionKind::EntryMarker, bar.open_time, *result.entry_price});
        open_segments(bar.open_time);
        event(StrategyEventKind::EntryFilled, *armed_time, bar.open_time, RunState::EntryArmed, RunState::Running,
              {definition().entry.condition->id}, {}, *result.entry_price, std::move(pending_evidence),
              "entry filled at next primary bar open");
    }

    void exit(Intern::Timestamp evaluated, Intern::Timestamp effective, double raw_price, ExitReason reason,
              std::vector<std::string> ids, std::vector<Evidence> evidence, std::string detail) {
        const auto selling = definition().direction == Direction::Long;
        result.exit_time = effective;
        result.exit_price = slipped(raw_price, !selling);
        result.current_price = result.exit_price;
        result.exit_reason = reason;
        extend_segments(effective);
        result.projection.markers.push_back({ProjectionKind::ExitMarker, effective, *result.exit_price});
        event(StrategyEventKind::ExitTriggered, evaluated, effective, result.state, RunState::Exited, std::move(ids),
              {}, *result.exit_price, std::move(evidence), std::move(detail));
        complete_result();
    }

    void complete_result() {
        if (!result.entry_price || !result.exit_price)
            return;

        const auto direction = definition().direction == Direction::Long ? 1.0 : -1.0;
        result.gross_profit_loss = direction * (*result.exit_price - *result.entry_price) * costs.quantity;
        const auto turnover = (*result.entry_price + *result.exit_price) * costs.quantity;
        const auto transaction_cost = 2.0 * costs.fixed_per_fill + turnover * costs.percentage_per_fill / 100.0;
        result.net_profit_loss = result.gross_profit_loss - transaction_cost;
        const auto base = costs.starting_capital.value_or(*result.entry_price * costs.quantity);
        result.return_percentage = base == 0 ? 0 : result.net_profit_loss / base * 100.0;
        result.duration = std::chrono::duration_cast<Duration>(*result.exit_time - *result.entry_time);
        result.elapsed = result.duration;
    }

    bool intrabar_exit(const Intern::Bar& bar) {
        const bool long_run = definition().direction == Direction::Long;
        const bool stop_gap = long_run ? bar.open <= *stop_price : bar.open >= *stop_price;
        const bool target_gap = long_run ? bar.open >= *target_price : bar.open <= *target_price;
        const bool stop_hit = stop_gap || (long_run ? bar.low <= *stop_price : bar.high >= *stop_price);
        const bool target_hit = target_gap || (long_run ? bar.high >= *target_price : bar.low <= *target_price);
        if (!stop_hit && !target_hit)
            return false;

        if (stop_hit && target_hit) {
            result.ambiguous_fill = true;
            result.warnings.push_back("stop and target touched in one OHLC bar; conservative stop fill used");
        }
        const bool use_stop = stop_hit;
        const auto gap = use_stop ? stop_gap : target_gap;
        const auto price = gap ? bar.open : (use_stop ? *stop_price : *target_price);
        exit(bar.open_time, bar.open_time, price, use_stop ? ExitReason::StopLoss : ExitReason::Target,
             {use_stop ? "stop-loss" : "target"}, {}, gap ? "gap fill at open" : "price level touched");

        return true;
    }

    void update_excursion(const Intern::Bar& bar) {
        const auto direction = definition().direction == Direction::Long ? 1.0 : -1.0;
        const auto favorable = direction > 0 ? bar.high - *result.entry_price : *result.entry_price - bar.low;
        const auto adverse = direction > 0 ? *result.entry_price - bar.low : bar.high - *result.entry_price;
        result.maximum_favorable_excursion = std::max(result.maximum_favorable_excursion, favorable * costs.quantity);
        result.maximum_adverse_excursion = std::max(result.maximum_adverse_excursion, adverse * costs.quantity);
        result.current_price = bar.close;
        result.unrealized_profit_loss = direction * (bar.close - *result.entry_price) * costs.quantity;
        result.unrealized_return_percentage =
            direction * (bar.close - *result.entry_price) / *result.entry_price * 100.0;
        result.elapsed = std::chrono::duration_cast<Duration>(*bar.close_time - *result.entry_time);
    }

    void arm_exit(const RuntimeRule& rule, const Evaluation& evaluation, Intern::Timestamp time, std::string reason) {
        pending_exit = ExitReason::RuntimeRule;
        pending_ids = {rule.id,
                       evaluation.evidence.empty() ? rule.condition->id : evaluation.evidence.front().condition_id};
        pending_evidence = evaluation.evidence;
        event(StrategyEventKind::ExitArmed, time, time, RunState::Running, RunState::Running, pending_ids, {}, {},
              pending_evidence, std::move(reason));
    }

    void runtime_rules(Intern::Timestamp time, double current) {
        auto rules = definition().runtime_rules;
        std::ranges::stable_sort(rules, {}, &RuntimeRule::priority);
        for (const auto& rule : rules) {
            const auto evaluation = graph->evaluate(rule.condition, time, values(time, current));
            if (evaluation.truth != Truth::True)
                continue;

            for (const auto& action : rule.actions) {
                if (action.kind == ActionKind::Exit) {
                    arm_exit(rule, evaluation, time, action.exit_reason);
                    return;
                }

                const auto next = action.price ? policy(*action.price, time, true) : std::nullopt;
                if (!next) {
                    fail(time, "runtime adjustment price is unavailable");
                    return;
                }

                auto& current_value = action.kind == ActionKind::AdjustStop ? stop_price : target_price;
                if (action.kind == ActionKind::AdjustStop) {
                    const bool tighter =
                        definition().direction == Direction::Long ? *next >= *current_value : *next <= *current_value;
                    if (!tighter) {
                        result.warnings.push_back("non-tightening stop adjustment rejected: " + rule.id);
                        continue;
                    }
                }

                const auto old = *current_value;
                current_value = next;
                const auto kind = action.kind == ActionKind::AdjustStop ? StrategyEventKind::StopAdjusted
                                                                        : StrategyEventKind::TargetAdjusted;
                const auto projection =
                    action.kind == ActionKind::AdjustStop ? ProjectionKind::StopPrice : ProjectionKind::TargetPrice;
                adjust_segment(projection, time, *next);
                event(kind, time, time, RunState::Running, RunState::Running, {rule.id, rule.condition->id}, old, *next,
                      evaluation.evidence);
            }
        }
    }

    void process(const Intern::Bar& bar) {
        if (result.state == RunState::Error || result.state == RunState::Exited || result.state == RunState::Stopped ||
            bar.state != Market::Core::Series::BarState::Closed || !bar.close_time)
            return;

        if (last_time && *bar.close_time <= *last_time)
            return;

        last_time = *bar.close_time;

        if (result.state == RunState::EntryArmed)
            enter(bar);
        if (result.state == RunState::Running && pending_exit != ExitReason::None) {
            exit(*armed_time, bar.open_time, bar.open, pending_exit, std::move(pending_ids),
                 std::move(pending_evidence), "condition exit filled at next primary bar open");
            return;
        }

        if (result.state == RunState::Running && intrabar_exit(bar))
            return;

        if (result.state == RunState::Running) {
            ++result.closed_bar_count;
            update_excursion(bar);
            extend_segments(*bar.close_time);
            for (const auto& configured : definition().exits) {
                const auto evaluation =
                    graph->evaluate(configured.condition, *bar.close_time, values(*bar.close_time, bar.close));
                if (evaluation.truth == Truth::True) {
                    pending_exit = ExitReason::Condition;
                    pending_ids = {configured.id, configured.condition->id};
                    pending_evidence = evaluation.evidence;
                    armed_time = *bar.close_time;
                    event(StrategyEventKind::ExitArmed, *bar.close_time, *bar.close_time, RunState::Running,
                          RunState::Running, pending_ids, {}, {}, pending_evidence, "explicit exit condition");
                    return;
                }
            }

            runtime_rules(*bar.close_time, bar.close);
            if (pending_exit != ExitReason::None)
                armed_time = *bar.close_time;
            return;
        }

        const auto evaluation = graph->evaluate(definition().entry.condition, *bar.close_time);
        if (evaluation.truth == Truth::True) {
            armed_time = *bar.close_time;
            pending_evidence = evaluation.evidence;
            event(StrategyEventKind::EntryArmed, *bar.close_time, *bar.close_time, RunState::WaitingForEntry,
                  RunState::EntryArmed, {definition().entry.condition->id}, {}, {}, evaluation.evidence,
                  "entry decision awaits next primary bar open");
        }
    }

    RunResult finish() {
        if (result.state == RunState::Running && last_time) {
            const auto bars = graph->primary_bars();
            const auto found = std::ranges::find_if(bars, [&](const auto& bar) { return bar.close_time == last_time; });
            if (found != bars.end())
                exit(*last_time, *last_time, found->close, ExitReason::EndOfRange, {"end-of-range"}, {},
                     "closed at end of test range");
        }

        return result;
    }
};

Engine::Engine(Snapshot snapshot, DataGraph& graph, ExecutionCosts costs)
    : m_implementation(std::make_unique<Implementation>(std::move(snapshot), graph, costs)) {}
Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;

Engine& Engine::operator=(Engine&&) noexcept = default;

RunState Engine::state() const {
    return m_implementation->result.state;
}

const RunResult& Engine::result() const {
    return m_implementation->result;
}

void Engine::process(const Market::Core::Series::Bar& bar) {
    m_implementation->process(bar);
}

void Engine::stop(Market::Core::Time::UtcTimestamp time, std::string detail) {
    auto& implementation = *m_implementation;
    if (implementation.result.state == RunState::Running && implementation.result.entry_price) {
        const auto bars = implementation.graph->primary_bars();
        const auto found = std::ranges::find_if(
            bars.rbegin(), bars.rend(), [&](const auto& bar) { return bar.close_time && *bar.close_time <= time; });
        const auto price = found == bars.rend() ? *implementation.result.entry_price : found->close;
        implementation.exit(time, time, price, ExitReason::UserStop, {"user-stop"}, {}, std::move(detail));
    } else {
        implementation.event(StrategyEventKind::RunStopped, time, time, implementation.result.state, RunState::Stopped,
                             {"user-stop"}, {}, {}, {}, std::move(detail));
        implementation.result.exit_reason = ExitReason::UserStop;
    }
}

RunResult Engine::finish() {
    return m_implementation->finish();
}

RunResult Engine::replay() {
    std::vector<Market::Core::Series::Bar> bars(m_implementation->graph->primary_bars().begin(),
                                                m_implementation->graph->primary_bars().end());
    std::ranges::stable_sort(bars, Intern::compare_time);
    for (const auto& bar : bars)
        process(bar);

    return finish();
}
} // namespace Didrachma::Strategy::Core
