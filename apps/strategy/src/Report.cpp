#include "Report.h"

#include "UtcTime.h"

#include <nlohmann/json.hpp>

namespace Didrachma::Apps::Strategy {
namespace {
using namespace Didrachma::Strategy::Core;

const char* state_name(RunState value) {
    switch (value) {
        case RunState::WaitingForEntry:
            return "waiting_for_entry";
        case RunState::EntryArmed:
            return "entry_armed";
        case RunState::Running:
            return "running";
        case RunState::Exited:
            return "exited";
        case RunState::Stopped:
            return "stopped";
        case RunState::Error:
            return "error";
    }

    return "error";
}

const char* event_name(StrategyEventKind value) {
    switch (value) {
        case StrategyEventKind::EntryArmed:
            return "entry_armed";
        case StrategyEventKind::EntryFilled:
            return "entry_filled";
        case StrategyEventKind::EntryExpired:
            return "entry_expired";
        case StrategyEventKind::StopAdjusted:
            return "stop_adjusted";
        case StrategyEventKind::TargetAdjusted:
            return "target_adjusted";
        case StrategyEventKind::ExitArmed:
            return "exit_armed";
        case StrategyEventKind::ExitTriggered:
            return "exit_triggered";
        case StrategyEventKind::RunStopped:
            return "run_stopped";
        case StrategyEventKind::Error:
            return "error";
    }

    return "error";
}

const char* exit_name(ExitReason value) {
    switch (value) {
        case ExitReason::None:
            return "none";
        case ExitReason::Condition:
            return "condition";
        case ExitReason::RuntimeRule:
            return "runtime_rule";
        case ExitReason::StopLoss:
            return "stop_loss";
        case ExitReason::Target:
            return "target";
        case ExitReason::UserStop:
            return "user_stop";
        case ExitReason::EndOfRange:
            return "end_of_range";
        case ExitReason::Error:
            return "error";
    }

    return "error";
}

const char* readiness_name(Readiness value) {
    switch (value) {
        case Readiness::Loading:
            return "loading";
        case Readiness::Ready:
            return "ready";
        case Readiness::Stale:
            return "stale";
        case Readiness::InsufficientHistory:
            return "insufficient_history";
        case Readiness::ProviderError:
            return "provider_error";
        case Readiness::CalculationError:
            return "calculation_error";
    }

    return "provider_error";
}

const char* orchestration_name(BacktestStatus value) {
    switch (value) {
        case BacktestStatus::Preparing:
            return "preparing";
        case BacktestStatus::Ready:
            return "ready";
        case BacktestStatus::Executing:
            return "executing";
        case BacktestStatus::Completed:
            return "completed";
        case BacktestStatus::Failed:
            return "failed";
        case BacktestStatus::Cancelled:
            return "cancelled";
    }
    return "failed";
}

const char* outcome_name(BacktestResultStatus value) {
    switch (value) {
        case BacktestResultStatus::NoSignal:
            return "no_signal";
        case BacktestResultStatus::SignalNotFilled:
            return "signal_not_filled";
        case BacktestResultStatus::OpenPositionClosedAtEnd:
            return "open_position_closed_at_end";
        case BacktestResultStatus::Exited:
            return "exited";
        case BacktestResultStatus::Failed:
            return "failed";
    }
    return "failed";
}

const char* entry_name(EntryStatus value) {
    switch (value) {
        case EntryStatus::NoSignal:
            return "no_signal";
        case EntryStatus::AwaitingFill:
            return "awaiting_fill";
        case EntryStatus::Expired:
            return "expired";
        case EntryStatus::Filled:
            return "filled";
    }
    return "no_signal";
}

const char* truth_name(Truth value) {
    switch (value) {
        case Truth::False:
            return "false";
        case Truth::True:
            return "true";
        case Truth::Unknown:
            return "unknown";
    }

    return "unknown";
}

const char* projection_name(ProjectionKind value) {
    switch (value) {
        case ProjectionKind::EntryMarker:
            return "entry_marker";
        case ProjectionKind::ExitMarker:
            return "exit_marker";
        case ProjectionKind::EntryPrice:
            return "entry_price";
        case ProjectionKind::StopPrice:
            return "stop_price";
        case ProjectionKind::TargetPrice:
            return "target_price";
    }

    return "entry_marker";
}

nlohmann::json evidence_json(const Evidence& evidence) {
    return {{"conditionId", evidence.condition_id},
            {"truth", truth_name(evidence.truth)},
            {"value", evidence.value},
            {"sourceTime", evidence.source_time ? nlohmann::json(format_utc(*evidence.source_time)) : nlohmann::json{}},
            {"detail", evidence.detail}};
}

std::string frame_name(Market::Core::Time::Frame frame) {
    const auto unit = frame.unit == Market::Core::Time::Unit::Minute ? "minute"
                      : frame.unit == Market::Core::Time::Unit::Hour ? "hour"
                                                                     : "day";
    return std::to_string(frame.quantity) + " " + unit + (frame.quantity == 1 ? "" : "s");
}
} // namespace

nlohmann::json make_report(const BacktestOutcome& outcome, const ReportContext& context) {
    const auto& definition = outcome.request.strategy_snapshot;
    const auto& result = *outcome.result;
    nlohmann::json inputs = nlohmann::json::array();
    for (const auto& item : outcome.inputs) {
        inputs.push_back({{"bindingId", item.series.binding_id},
                          {"providerId", item.series.key.provider},
                          {"instrument", item.series.key.instrument},
                          {"timeframe", frame_name(item.series.key.timeframe)},
                          {"readiness", readiness_name(item.state.readiness)},
                          {"detail", item.state.detail},
                          {"availableHistory", item.state.available_history},
                          {"requiredHistory", item.state.required_history}});
    }

    nlohmann::json events = nlohmann::json::array();
    for (const auto& event : result.events) {
        nlohmann::json evidence = nlohmann::json::array();
        for (const auto& item : event.evidence)
            evidence.push_back(evidence_json(item));
        events.push_back({{"kind", event_name(event.kind)},
                          {"oldState", state_name(event.old_state)},
                          {"newState", state_name(event.new_state)},
                          {"evaluationTime", format_utc(event.evaluation_time)},
                          {"effectiveTime", format_utc(event.effective_time)},
                          {"triggerIds", event.trigger_ids},
                          {"oldValue", event.old_value},
                          {"newValue", event.new_value},
                          {"detail", event.detail},
                          {"evidence", std::move(evidence)}});
    }

    nlohmann::json markers = nlohmann::json::array();
    for (const auto& marker : result.projection.markers)
        markers.push_back(
            {{"kind", projection_name(marker.kind)}, {"time", format_utc(marker.time)}, {"price", marker.price}});
    nlohmann::json segments = nlohmann::json::array();
    for (const auto& segment : result.projection.segments)
        segments.push_back({{"kind", projection_name(segment.kind)},
                            {"begin", format_utc(segment.begin)},
                            {"end", format_utc(segment.end)},
                            {"price", segment.price}});

    const auto status = outcome.result_status == BacktestResultStatus::NoSignal          ? "no_entry"
                        : outcome.result_status == BacktestResultStatus::SignalNotFilled ? "signal_not_filled"
                        : outcome.result_status == BacktestResultStatus::OpenPositionClosedAtEnd
                            ? "open_position_closed_at_end"
                            : state_name(result.state);
    return {{"reportFormatVersion", 2},
            {"strategy", {{"id", definition.id}, {"version", definition.version}, {"file", context.strategy_file}}},
            {"request",
             {{"provider", context.provider},
              {"providerConfiguration", outcome.request.provider.values},
              {"subject", context.subject},
              {"from", format_utc(context.from)},
              {"throughInclusive", format_utc(context.through)},
              {"fillModelVersion", outcome.request.fill_model_version},
              {"effectiveWarmupBegin",
               outcome.warmup_begin ? nlohmann::json(format_utc(*outcome.warmup_begin)) : nlohmann::json{}}}},
            {"execution",
             {{"quantity", context.costs.quantity},
              {"startingCapital", context.costs.starting_capital},
              {"fixedCostPerFill", context.costs.fixed_per_fill},
              {"percentageCostPerFill", context.costs.percentage_per_fill},
              {"slippagePercentage", context.costs.slippage_percentage},
              {"overridden", context.costs_overridden}}},
            {"inputs", std::move(inputs)},
            {"orchestrationStatus", orchestration_name(outcome.status)},
            {"outcomeStatus", outcome_name(outcome.result_status)},
            {"result",
             {{"status", status},
              {"state", state_name(result.state)},
              {"entryStatus", entry_name(result.entry_status)},
              {"exitReason", exit_name(result.exit_reason)},
              {"entryTime", result.entry_time ? nlohmann::json(format_utc(*result.entry_time)) : nlohmann::json{}},
              {"exitTime", result.exit_time ? nlohmann::json(format_utc(*result.exit_time)) : nlohmann::json{}},
              {"entryPrice", result.entry_price},
              {"exitPrice", result.exit_price},
              {"currentPrice", result.current_price},
              {"unrealizedProfitLoss", result.unrealized_profit_loss},
              {"unrealizedReturnPercentage", result.unrealized_return_percentage},
              {"grossProfitLoss", result.gross_profit_loss},
              {"netProfitLoss", result.net_profit_loss},
              {"returnPercentage", result.return_percentage},
              {"durationSeconds", result.duration.count()},
              {"elapsedSeconds", result.elapsed.count()},
              {"closedBarCount", result.closed_bar_count},
              {"maximumFavorableExcursion", result.maximum_favorable_excursion},
              {"maximumAdverseExcursion", result.maximum_adverse_excursion},
              {"triggerCounts", result.trigger_counts},
              {"ambiguousFill", result.ambiguous_fill},
              {"warnings", result.warnings},
              {"events", std::move(events)},
              {"projection", {{"markers", std::move(markers)}, {"segments", std::move(segments)}}}}}};
}

void print_summary(const nlohmann::json& report, bool verbose, std::ostream& output) {
    output << "Strategy: " << report["strategy"]["id"].get<std::string>() << '\n';
    for (const auto& input : report["inputs"])
        output << "Input " << input["bindingId"].get<std::string>() << ": " << input["instrument"].get<std::string>()
               << " @ " << input["timeframe"].get<std::string>() << " [" << input["readiness"].get<std::string>()
               << "]\n";
    if (verbose)
        for (const auto& event : report["result"]["events"])
            output << "Event " << event["kind"].get<std::string>() << " @ " << event["effectiveTime"].get<std::string>()
                   << ": " << event["detail"].get<std::string>() << '\n';
    const auto& result = report["result"];
    output << "Result: " << result["status"].get<std::string>() << " (" << result["exitReason"].get<std::string>()
           << ")\nGross P/L: " << result["grossProfitLoss"] << "\nNet P/L: " << result["netProfitLoss"]
           << "\nReturn: " << result["returnPercentage"] << "%\nDuration: " << result["durationSeconds"]
           << " seconds\n";
    for (const auto& warning : result["warnings"])
        output << "Warning: " << warning.get<std::string>() << '\n';
}
} // namespace Didrachma::Apps::Strategy
