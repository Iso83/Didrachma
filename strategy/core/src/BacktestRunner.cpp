#include <Didrachma/strategy/core/Backtest.h>
#include <algorithm>
#include <chrono>

namespace Didrachma::Strategy::Core::Intern {
BacktestResultStatus status_of(const RunResult& result) {
    if (result.entry_status == EntryStatus::NoSignal)
        return BacktestResultStatus::NoSignal;

    if (result.entry_status == EntryStatus::AwaitingFill || result.entry_status == EntryStatus::Expired)
        return BacktestResultStatus::SignalNotFilled;

    if (result.exit_reason == ExitReason::EndOfRange)
        return BacktestResultStatus::OpenPositionClosedAtEnd;

    return result.state == RunState::Error ? BacktestResultStatus::Failed : BacktestResultStatus::Exited;
}

void append_occurrence(BacktestOutcome& outcome, RunResult result) {
    if (result.entry_status == EntryStatus::NoSignal) {
        for (auto& evaluation : result.evaluations)
            outcome.evaluations.push_back({std::nullopt, std::move(evaluation)});
        for (auto& event : result.events)
            outcome.audit_events.push_back({std::nullopt, std::move(event)});
        return;
    }

    const auto status = status_of(result);
    const auto occurrence_id = static_cast<std::uint64_t>(outcome.occurrences.size());
    for (const auto& evaluation : result.evaluations)
        outcome.evaluations.push_back({occurrence_id, evaluation});
    for (const auto& event : result.events)
        outcome.audit_events.push_back({occurrence_id, event});
    outcome.occurrences.push_back({occurrence_id, status, std::move(result)});
    const auto& stored = outcome.occurrences.back().result;
    ++outcome.summary.signal_count;
    if (stored.entry_status == EntryStatus::Filled)
        ++outcome.summary.filled_count;
    if (stored.exit_reason != ExitReason::None) {
        ++outcome.summary.closed_count;
        outcome.summary.target_count += stored.exit_reason == ExitReason::Target;
        outcome.summary.stop_count += stored.exit_reason == ExitReason::StopLoss;
        outcome.summary.end_of_range_count += stored.exit_reason == ExitReason::EndOfRange;
        outcome.summary.gross_profit_loss += stored.gross_profit_loss;
        outcome.summary.net_profit_loss += stored.net_profit_loss;
        outcome.summary.costs += stored.gross_profit_loss - stored.net_profit_loss;
    }
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

} // namespace Didrachma::Strategy::Core::Intern

namespace Didrachma::Strategy::Core {
BacktestOutcome BacktestRunner::run(const BacktestRequest& request, Market::Core::Provider::Data& provider,
                                    std::span<const Market::Core::Time::Frame> supported_timeframes,
                                    std::function<bool()> cancelled, BacktestProgressObserver progress) const {
    std::vector<BacktestSourceCapability> capabilities;
    for (const auto frame : supported_timeframes)
        capabilities.push_back({frame, frame, std::nullopt});

    return run(request, provider, capabilities,
               std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()),
               std::move(cancelled), std::move(progress));
}

BacktestOutcome BacktestRunner::run(const BacktestRequest& request, Market::Core::Provider::Data& provider,
                                    std::span<const BacktestSourceCapability> capabilities,
                                    Market::Core::Time::UtcTimestamp now, std::function<bool()> cancelled,
                                    BacktestProgressObserver progress) const {
    using namespace Intern;
    using Clock = std::chrono::steady_clock;
    const auto runner_started = Clock::now();
    auto stage_started = runner_started;
    enum class TimedStage { None, DataPreparation, IndicatorCalculation, StrategyExecution };
    auto timed_stage = TimedStage::None;
    BacktestOutcome outcome;
    outcome.request = request;
    outcome.request.strategy_snapshot = Snapshot{request.strategy_snapshot}.definition();
    const auto is_cancelled = [&] { return cancelled && cancelled(); };
    const auto publish = [&](BacktestProgress value) {
        if (progress)
            progress(value);
    };
    const auto finish_stage = [&]() {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - stage_started);
        if (timed_stage == TimedStage::DataPreparation)
            outcome.timings.data_preparation = elapsed;
        else if (timed_stage == TimedStage::IndicatorCalculation)
            outcome.timings.indicator_calculation = elapsed;
        else if (timed_stage == TimedStage::StrategyExecution)
            outcome.timings.strategy_execution = elapsed;
        timed_stage = TimedStage::None;
    };
    const auto start_stage = [&](TimedStage stage) {
        stage_started = Clock::now();
        timed_stage = stage;
    };
    const auto finish = [&]() {
        finish_stage();
        outcome.timings.total = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - runner_started);
        publish({outcome.status == BacktestStatus::Completed   ? BacktestProgressKind::Completed
                 : outcome.status == BacktestStatus::Cancelled ? BacktestProgressKind::Cancelled
                                                               : BacktestProgressKind::Failed});
        return outcome;
    };
    publish({BacktestProgressKind::Preparing});
    start_stage(TimedStage::DataPreparation);

    const auto plan = plan_backtest(request, *m_analyzer, capabilities, now);
    if (!plan.errors.empty()) {
        outcome.input_errors = plan.resolution_errors;
        outcome.errors = plan.errors;
        transition(outcome, BacktestStatus::Failed);
        return finish();
    }

    if (!provider.capabilities().supports(Market::Core::Provider::Capability::History)) {
        fail(outcome, BacktestErrorCode::ProviderFailure, "/provider", "Provider does not support history loading");
        return finish();
    }

    DataGraph graph{Snapshot{request.strategy_snapshot}, *m_analyzer, request.subject_symbol};
    for (const auto& [target, source] : plan.derivations)
        graph.derive_series(target, source);

    for (const auto& resolved : graph.resolution().series)
        outcome.inputs.push_back({resolved, graph.state(resolved.binding_id)});

    constexpr int maximum_attempts = 5;
    for (const auto& group : plan.loads) {
        for (const auto& binding : group.binding_ids)
            publish({BacktestProgressKind::Loading, binding});
        if (is_cancelled()) {
            fail(outcome, BacktestErrorCode::Cancelled, "/", "Backtest cancelled while preparing inputs");
            return finish();
        }

        auto lookback = request.from - group.load_begin;
        bool ready = false;
        for (int attempt = 1; attempt <= maximum_attempts; ++attempt) {
            Market::Core::Provider::HistoryResult loaded;
            try {
                loaded = provider.load_history({group.request_key, {request.from - lookback, group.exclusive_end}});
            } catch (const std::exception& error) {
                fail(outcome, BacktestErrorCode::ProviderFailure, group.binding_ids.front(), error.what());
                return finish();
            }

            if (const auto* error = std::get_if<Market::Core::Provider::Error>(&loaded)) {
                fail(outcome, BacktestErrorCode::ProviderFailure, group.binding_ids.front(), error->message);
                return finish();
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
            const auto dependencies_ready = std::ranges::all_of(group.dependent_binding_ids, [&](const auto& id) {
                const auto required = graph.required_history(id);
                const auto dependent = graph.series_bars(id);
                const auto dependent_warmup = std::ranges::count_if(dependent, [&](const auto& bar) {
                    return bar.state == Market::Core::Series::BarState::Closed && bar.close_time &&
                           *bar.close_time < request.from;
                });
                return dependent.size() >= required && dependent_warmup >= required - 1;
            });
            if (bars.size() >= group.required_history && warmup_bars >= group.required_history - 1 &&
                dependencies_ready) {
                ready = true;
                break;
            }
            lookback *= 2;
        }
        if (!ready) {
            fail(outcome, BacktestErrorCode::InsufficientWarmup, group.binding_ids.front(),
                 "insufficient history after bounded warm-up retries; required " +
                     std::to_string(group.required_history) + " bars");
            return finish();
        }
    }

    finish_stage();
    start_stage(TimedStage::IndicatorCalculation);
    try {
        (void)graph.evaluate_entry();
    } catch (const std::exception& error) {
        fail(outcome, BacktestErrorCode::CalculationFailure, "/inputs", error.what());
        return finish();
    }
    finish_stage();
    for (auto& input : outcome.inputs) {
        input.state = graph.state(input.series.binding_id);
        if (input.state.readiness != Readiness::Ready) {
            const auto code = input.state.readiness == Readiness::InsufficientHistory
                                  ? BacktestErrorCode::InsufficientWarmup
                                  : BacktestErrorCode::CalculationFailure;
            fail(outcome, code, input.series.binding_id,
                 input.state.detail.empty() ? "Required input is not ready" : input.state.detail);
            return finish();
        }
        publish({BacktestProgressKind::Ready, input.series.binding_id, input.state});
        const auto bars = graph.series_bars(input.series.binding_id);
        input.bars.assign(bars.begin(), bars.end());
    }

    const auto primary = graph.primary_bars();
    const auto in_range = [&](const auto& bar) {
        return bar.state == Market::Core::Series::BarState::Closed && bar.close_time &&
               *bar.close_time >= request.from && *bar.close_time <= request.through;
    };
    if (std::ranges::none_of(primary, in_range)) {
        fail(outcome, BacktestErrorCode::NoBarsInRange, "/range",
             "No closed primary bars have close times in the inclusive requested range");
        return finish();
    }

    transition(outcome, BacktestStatus::Ready);
    transition(outcome, BacktestStatus::Executing);
    publish({BacktestProgressKind::Executing});
    start_stage(TimedStage::StrategyExecution);
    try {
        auto engine = std::make_unique<Engine>(Snapshot{request.strategy_snapshot}, graph, request.execution);
        for (const auto& bar : primary) {
            if (is_cancelled()) {
                fail(outcome, BacktestErrorCode::Cancelled, "/", "Backtest cancelled during execution");
                return finish();
            }

            if (!in_range(bar))
                continue;

            engine->process(bar);
            if (engine->state() == RunState::Exited) {
                append_occurrence(outcome, engine->finish());
                engine = std::make_unique<Engine>(Snapshot{request.strategy_snapshot}, graph, request.execution);
                // An exit at this bar's open/intrabar may be followed by a new
                // close decision. A next-bar-open order cannot fill here.
                engine->process(bar);
            }
        }
        auto final = engine->finish();
        if (final.state == RunState::Error) {
            append_occurrence(outcome, std::move(final));
            fail(outcome, BacktestErrorCode::EngineFailure, "/result", "Strategy engine reported an error");
            return finish();
        }

        append_occurrence(outcome, std::move(final));
    } catch (const std::exception& error) {
        fail(outcome, BacktestErrorCode::EngineFailure, "/result", error.what());
        return finish();
    }
    if (outcome.occurrences.empty())
        outcome.result_status = BacktestResultStatus::NoSignal;
    else if (outcome.summary.closed_count)
        outcome.result_status = outcome.occurrences[static_cast<std::size_t>(outcome.summary.closed_count - 1)].status;
    else
        outcome.result_status = BacktestResultStatus::SignalNotFilled;
    finish_stage();
    transition(outcome, BacktestStatus::Completed);
    return finish();
}
} // namespace Didrachma::Strategy::Core
