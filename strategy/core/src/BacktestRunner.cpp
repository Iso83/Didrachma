#include <Didrachma/strategy/core/Backtest.h>
#include <algorithm>
#include <map>

namespace Didrachma::Strategy::Core::Intern {
struct LoadGroup {
    Market::Core::Series::Key key;
    std::vector<std::string> binding_ids;
    std::size_t required{};
};

std::string backtest_key_text(const Market::Core::Series::Key& key) {
    return key.provider + '\n' + key.instrument + '\n' + std::to_string(key.timeframe.quantity) + '\n' +
           std::to_string(static_cast<int>(key.timeframe.unit));
}

Duration backtest_frame_duration(Market::Core::Time::Frame frame) {
    using enum Market::Core::Time::Unit;
    const auto unit = frame.unit == Minute ? std::chrono::minutes{1}
                      : frame.unit == Hour ? std::chrono::hours{1}
                                           : std::chrono::hours{24};
    return std::chrono::duration_cast<Duration>(unit) * frame.quantity;
}

std::vector<LoadGroup> load_plan(const DataGraph& graph) {
    std::map<std::string, LoadGroup> grouped;
    for (const auto& resolved : graph.resolution().series) {
        auto& group = grouped[backtest_key_text(resolved.key)];
        group.key = resolved.key;
        group.binding_ids.push_back(resolved.binding_id);
        group.required =
            std::max(group.required, std::max<std::size_t>(graph.required_history(resolved.binding_id), 1));
    }
    std::vector<LoadGroup> result;
    for (auto& [key, group] : grouped) {
        (void)key;
        result.push_back(std::move(group));
    }
    return result;
}

void transition(BacktestOutcome& outcome, BacktestStatus status) {
    outcome.status = status;
    outcome.status_history.push_back(status);
}

void fail(BacktestOutcome& outcome, BacktestErrorCode code, std::string path, std::string message) {
    outcome.errors.push_back({code, std::move(path), std::move(message)});
    outcome.result_status = BacktestResultStatus::Failed;
    transition(outcome, code == BacktestErrorCode::Cancelled ? BacktestStatus::Cancelled : BacktestStatus::Failed);
}

bool supported(Market::Core::Time::Frame frame, std::span<const Market::Core::Time::Frame> capabilities) {
    return std::ranges::find(capabilities, frame) != capabilities.end();
}
} // namespace Didrachma::Strategy::Core::Intern

namespace Didrachma::Strategy::Core {
BacktestOutcome BacktestRunner::run(const BacktestRequest& request, Market::Core::Provider::Data& provider,
                                    std::span<const Market::Core::Time::Frame> supported_timeframes,
                                    std::function<bool()> cancelled) const {
    using namespace Intern;
    BacktestOutcome outcome;
    outcome.request = request;
    outcome.request.strategy_snapshot = Snapshot{request.strategy_snapshot}.definition();
    const auto is_cancelled = [&] { return cancelled && cancelled(); };

    const auto invalid = validate(request, m_analyzer->catalog());
    if (!invalid.empty()) {
        for (const auto& error : invalid) {
            const auto code = error.path == "/subjectSymbol" ? BacktestErrorCode::UnresolvedSubject
                                                             : BacktestErrorCode::InvalidRequest;
            outcome.errors.push_back({code, error.path, error.message});
        }
        transition(outcome, BacktestStatus::Failed);
        return outcome;
    }

    if (!provider.capabilities().supports(Market::Core::Provider::Capability::History)) {
        fail(outcome, BacktestErrorCode::ProviderFailure, "/provider", "Provider does not support history loading");
        return outcome;
    }

    for (std::size_t index = 0; index < request.strategy_snapshot.series.size(); ++index) {
        const auto& binding = request.strategy_snapshot.series[index];
        if (binding.provider_id != request.provider.id) {
            fail(outcome, BacktestErrorCode::ProviderFailure,
                 "/strategy/series/" + std::to_string(index) + "/providerId",
                 "Series provider does not match the requested provider");
            return outcome;
        }

        if (!supported(binding.timeframe, supported_timeframes)) {
            fail(outcome, BacktestErrorCode::UnsupportedTimeframe,
                 "/strategy/series/" + std::to_string(index) + "/timeframe", "Provider does not support timeframe");
            return outcome;
        }
    }

    DataGraph graph{Snapshot{request.strategy_snapshot}, *m_analyzer, request.subject_symbol};
    outcome.input_errors = graph.resolution().errors;
    if (!outcome.input_errors.empty()) {
        for (const auto& error : outcome.input_errors)
            outcome.errors.push_back({BacktestErrorCode::UnresolvedSubject, error.binding_id, error.message});
        transition(outcome, BacktestStatus::Failed);
        return outcome;
    }

    for (const auto& resolved : graph.resolution().series)
        outcome.inputs.push_back({resolved, graph.state(resolved.binding_id)});

    constexpr int maximum_attempts = 5;
    for (const auto& group : load_plan(graph)) {
        if (is_cancelled()) {
            fail(outcome, BacktestErrorCode::Cancelled, "/", "Backtest cancelled while preparing inputs");
            return outcome;
        }

        auto lookback = backtest_frame_duration(group.key.timeframe) *
                        static_cast<std::int64_t>(std::max<std::size_t>(group.required * 2, 4));
        bool ready = false;
        for (int attempt = 1; attempt <= maximum_attempts; ++attempt) {
            Market::Core::Provider::HistoryResult loaded;
            try {
                loaded = provider.load_history(
                    {group.key, {request.from - lookback, request.through + std::chrono::seconds{1}}});
            } catch (const std::exception& error) {
                fail(outcome, BacktestErrorCode::ProviderFailure, group.binding_ids.front(), error.what());
                return outcome;
            }
            if (const auto* error = std::get_if<Market::Core::Provider::Error>(&loaded)) {
                fail(outcome, BacktestErrorCode::ProviderFailure, group.binding_ids.front(), error->message);
                return outcome;
            }

            const auto& bars = std::get<std::vector<Market::Core::Series::Bar>>(loaded);
            for (const auto& bar : bars)
                if (!outcome.warmup_begin || bar.open_time < *outcome.warmup_begin)
                    outcome.warmup_begin = bar.open_time;
            for (const auto& binding : group.binding_ids)
                graph.set_series(binding, bars, static_cast<std::uint64_t>(attempt));
            const auto warmup_bars = std::ranges::count_if(bars, [&](const auto& bar) {
                return bar.state == Market::Core::Series::BarState::Closed && bar.close_time &&
                       *bar.close_time < request.from;
            });
            if (bars.size() >= group.required && warmup_bars >= group.required - 1) {
                ready = true;
                break;
            }
            lookback *= 2;
        }
        if (!ready) {
            fail(outcome, BacktestErrorCode::InsufficientWarmup, group.binding_ids.front(),
                 "insufficient history after bounded warm-up retries; required " + std::to_string(group.required) +
                     " bars");
            return outcome;
        }
    }

    try {
        (void)graph.evaluate_entry();
    } catch (const std::exception& error) {
        fail(outcome, BacktestErrorCode::CalculationFailure, "/inputs", error.what());
        return outcome;
    }
    for (auto& input : outcome.inputs) {
        input.state = graph.state(input.series.binding_id);
        if (input.state.readiness != Readiness::Ready) {
            const auto code = input.state.readiness == Readiness::InsufficientHistory
                                  ? BacktestErrorCode::InsufficientWarmup
                                  : BacktestErrorCode::CalculationFailure;
            fail(outcome, code, input.series.binding_id,
                 input.state.detail.empty() ? "Required input is not ready" : input.state.detail);
            return outcome;
        }
    }

    const auto primary = graph.primary_bars();
    const auto in_range = [&](const auto& bar) {
        return bar.state == Market::Core::Series::BarState::Closed && bar.close_time &&
               *bar.close_time >= request.from && *bar.close_time <= request.through;
    };
    if (std::ranges::none_of(primary, in_range)) {
        fail(outcome, BacktestErrorCode::NoBarsInRange, "/range",
             "No closed primary bars have close times in the inclusive requested range");
        return outcome;
    }

    transition(outcome, BacktestStatus::Ready);
    transition(outcome, BacktestStatus::Executing);
    try {
        Engine engine{Snapshot{request.strategy_snapshot}, graph, request.execution};
        for (const auto& bar : primary) {
            if (is_cancelled()) {
                fail(outcome, BacktestErrorCode::Cancelled, "/", "Backtest cancelled during execution");
                return outcome;
            }

            if (in_range(bar))
                engine.process(bar);
        }
        outcome.result = engine.finish();
    } catch (const std::exception& error) {
        fail(outcome, BacktestErrorCode::EngineFailure, "/result", error.what());
        return outcome;
    }
    outcome.audit_events = outcome.result->events;
    if (outcome.result->state == RunState::Error) {
        fail(outcome, BacktestErrorCode::EngineFailure, "/result", "Strategy engine reported an error");
        return outcome;
    }

    if (outcome.result->entry_status == EntryStatus::NoSignal)
        outcome.result_status = BacktestResultStatus::NoSignal;
    else if (outcome.result->entry_status == EntryStatus::AwaitingFill ||
             outcome.result->entry_status == EntryStatus::Expired)
        outcome.result_status = BacktestResultStatus::SignalNotFilled;
    else if (outcome.result->exit_reason == ExitReason::EndOfRange)
        outcome.result_status = BacktestResultStatus::OpenPositionClosedAtEnd;
    else
        outcome.result_status = BacktestResultStatus::Exited;
    transition(outcome, BacktestStatus::Completed);
    return outcome;
}
} // namespace Didrachma::Strategy::Core
