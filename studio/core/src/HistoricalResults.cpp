#include <Didrachma/studio/core/HistoricalResults.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace Didrachma::Studio::Core::Intern {
using Timestamp = Market::Core::Time::UtcTimestamp;

std::string number(double value) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(2) << value;
    return text.str();
}

std::string milliseconds(const std::optional<std::chrono::nanoseconds>& value) {
    return value ? number(std::chrono::duration<double, std::milli>(*value).count()) + " ms" : "—";
}

std::string time(Timestamp value) {
    const auto raw = std::chrono::system_clock::to_time_t(value);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &raw);
#else
    gmtime_r(&raw, &utc);
#endif
    std::ostringstream text;
    text << std::put_time(&utc, "%Y-%m-%d %H:%M:%S UTC");
    return text.str();
}

const char* terminal(Strategy::Core::BacktestStatus status) {
    using enum Strategy::Core::BacktestStatus;
    switch (status) {
        case Preparing:
            return "Preparing";
        case Ready:
            return "Ready";
        case Executing:
            return "Executing";
        case Completed:
            return "Completed";
        case Failed:
            return "Failed";
        case Cancelled:
            return "Cancelled";
    }

    return "—";
}

const char* result(Strategy::Core::BacktestResultStatus status) {
    using enum Strategy::Core::BacktestResultStatus;
    switch (status) {
        case NoSignal:
            return "NoSignal";
        case SignalNotFilled:
            return "SignalNotFilled";
        case OpenPositionClosedAtEnd:
            return "OpenPositionClosedAtEnd";
        case Exited:
            return "Exited";
        case Failed:
            return "Failed";
    }

    return "—";
}

const char* exit_reason(Strategy::Core::ExitReason reason) {
    using enum Strategy::Core::ExitReason;
    switch (reason) {
        case None:
            return "—";
        case Condition:
            return "Condition";
        case RuntimeRule:
            return "RuntimeRule";
        case StopLoss:
            return "StopLoss";
        case Target:
            return "Target";
        case UserStop:
            return "UserStop";
        case EndOfRange:
            return "EndOfRange";
        case Error:
            return "Error";
    }

    return "—";
}
} // namespace Didrachma::Studio::Core::Intern

namespace Didrachma::Studio::Core {
std::string format_utc(Market::Core::Time::UtcTimestamp value) {
    return Intern::time(value);
}

std::string format_timeframe(Market::Core::Time::Frame value) {
    const auto unit = value.unit == Market::Core::Time::Unit::Minute ? "minute"
                      : value.unit == Market::Core::Time::Unit::Hour ? "hour"
                                                                     : "day";
    return std::to_string(value.quantity) + " " + unit + (value.quantity == 1 ? "" : "s");
}

std::string format_truth(Strategy::Core::Truth value) {
    return value == Strategy::Core::Truth::True ? "True" : value == Strategy::Core::Truth::False ? "False" : "Unknown";
}

std::string format_event_kind(Strategy::Core::StrategyEventKind value) {
    using enum Strategy::Core::StrategyEventKind;
    switch (value) {
        case EntryArmed:
            return "Entry armed";
        case EntryFilled:
            return "Entry filled";
        case EntryExpired:
            return "Entry expired";
        case StopAdjusted:
            return "Stop adjusted";
        case TargetAdjusted:
            return "Target adjusted";
        case ExitArmed:
            return "Exit armed";
        case ExitTriggered:
            return "Exit triggered";
        case RunStopped:
            return "Run stopped";
        case Error:
            return "Error";
    }
    return "Unknown event";
}

std::string format_exit_reason(Strategy::Core::ExitReason value) {
    return Intern::exit_reason(value);
}

HistoricalResultDisplay present(const HistoricalBacktest& run) {
    constexpr const char* absent = "—";
    HistoricalResultDisplay view;
    view.subject = run.request.subject_symbol.value_or(absent);
    view.inclusive_range = Intern::time(run.request.from) + " — " + Intern::time(run.request.through);
    if (!run.outcome) {
        view.terminal_status = Intern::terminal(Strategy::Core::BacktestStatus::Preparing);
        view.result_status = absent;
    } else {
        view.terminal_status = Intern::terminal(run.outcome->status);
        view.result_status = Intern::result(run.outcome->result_status);
    }
    view.entry_time = view.exit_time = view.entry_price = view.exit_price = view.exit_reason = absent;
    view.gross_profit_loss = view.net_profit_loss = view.return_percentage = view.duration = view.costs = absent;
    view.ambiguity = view.warnings = absent;
    view.data_preparation = view.indicator_calculation = view.strategy_execution = view.total_runner = absent;
    if (run.outcome) {
        view.data_preparation = Intern::milliseconds(run.outcome->timings.data_preparation);
        view.indicator_calculation = Intern::milliseconds(run.outcome->timings.indicator_calculation);
        view.strategy_execution = Intern::milliseconds(run.outcome->timings.strategy_execution);
        view.total_runner = Intern::milliseconds(run.outcome->timings.total);
    }
    if (!run.outcome)
        return view;

    view.gross_profit_loss = Intern::number(run.outcome->summary.gross_profit_loss);
    view.net_profit_loss = Intern::number(run.outcome->summary.net_profit_loss);
    view.costs = Intern::number(run.outcome->summary.costs);

    return view;
}

std::vector<HistoricalTimelineItem> timeline(const HistoricalBacktest& run) {
    std::vector<HistoricalTimelineItem> items;
    if (!run.outcome)
        return items;

    std::size_t ordinal{};
    for (const auto& owned : run.outcome->evaluations) {
        const auto& evaluation = owned.evaluation;
        for (const auto& evidence : evaluation.evidence) {
            items.push_back({"evidence-" + std::to_string(ordinal++), HistoricalTimelineKind::Evaluation,
                             evidence.source_time.value_or(evaluation.time), "Condition", evidence.condition_path,
                             evidence.truth, evidence.sequence_step, evidence.sequence_size, evidence.binding_id,
                             evidence.source_key, evidence.source_timeframe, evidence.value, evidence.detail});
            items.back().occurrence_id = owned.occurrence_id;
        }
    }
    for (const auto& audit : run.outcome->audit_events) {
        const auto& event = audit.event;
        const auto* source = event.evidence.empty() ? nullptr : &event.evidence.front();
        items.push_back({"event-" + std::to_string(ordinal++),
                         HistoricalTimelineKind::AuditEvent,
                         source && source->source_time ? *source->source_time : event.effective_time,
                         format_event_kind(event.kind),
                         event.trigger_ids.empty() ? "—" : event.trigger_ids.front(),
                         Strategy::Core::Truth::Unknown,
                         {},
                         {},
                         source ? source->binding_id : std::string{},
                         source ? source->source_key : std::optional<Market::Core::Series::Key>{},
                         source ? source->source_timeframe : std::optional<Market::Core::Time::Frame>{},
                         source ? source->value : std::optional<double>{},
                         event.detail});
        items.back().occurrence_id = audit.occurrence_id;
    }
    std::ranges::stable_sort(items, {}, &HistoricalTimelineItem::time);
    return items;
}

std::vector<HistoricalEvidenceGroup> evidence_groups(const HistoricalBacktest& run) {
    std::vector<HistoricalEvidenceGroup> groups;
    if (!run.outcome)
        return groups;

    const auto items = timeline(run);
    for (std::size_t index = 0; index < run.outcome->occurrences.size(); ++index) {
        const auto& occurrence = run.outcome->occurrences[index];
        const auto& result = occurrence.result;
        HistoricalEvidenceGroup group;
        group.occurrence_id = occurrence.id;
        group.occurrence_number = index + 1;
        group.label = "Occurrence #" + std::to_string(index + 1) + " (id " + std::to_string(occurrence.id) + ")";
        group.entry_summary = "Entry: " + (result.entry_price ? Intern::number(*result.entry_price) : "—") + " at " +
                              (result.entry_time ? format_utc(*result.entry_time) : "—");
        group.exit_summary = "Exit: " + (result.exit_price ? Intern::number(*result.exit_price) : "—") + " at " +
                             (result.exit_time ? format_utc(*result.exit_time) : "—") + " — " +
                             format_exit_reason(result.exit_reason) + " — net P/L " +
                             Intern::number(result.net_profit_loss);
        std::ranges::copy_if(items, std::back_inserter(group.items),
                             [&](const auto& item) { return item.occurrence_id == occurrence.id; });
        groups.push_back(std::move(group));
    }

    HistoricalEvidenceGroup run_level;
    run_level.label = "Run-level evidence";
    std::ranges::copy_if(items, std::back_inserter(run_level.items),
                         [](const auto& item) { return !item.occurrence_id; });
    if (!run_level.items.empty())
        groups.push_back(std::move(run_level));

    return groups;
}

} // namespace Didrachma::Studio::Core
