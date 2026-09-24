#pragma once

#include <Didrachma/strategy/core/Runtime.h>
#include <Didrachma/strategy/core/Validation.h>
#include <functional>

namespace Didrachma::Strategy::Core {
// A definition is reusable logic. A request binds its immutable definition
// snapshot to one subject, inclusive evaluation range, and execution assumptions.
struct ProviderConfiguration {
    std::string id;
    std::map<std::string, std::string> values;
};

struct BacktestRequest {
    Definition strategy_snapshot;
    std::optional<std::string> subject_symbol;
    Market::Core::Time::UtcTimestamp from;
    Market::Core::Time::UtcTimestamp through;
    ProviderConfiguration provider;
    ExecutionCosts execution;
    std::uint32_t fill_model_version{1};
};

struct BacktestInput {
    ResolvedSeries series;
    BindingState state;
};

enum class BacktestStatus { Preparing, Ready, Executing, Completed, Failed, Cancelled };
enum class BacktestResultStatus { NoSignal, SignalNotFilled, OpenPositionClosedAtEnd, Exited, Failed };
enum class BacktestErrorCode {
    InvalidRequest,
    UnresolvedSubject,
    UnsupportedTimeframe,
    ProviderFailure,
    InsufficientWarmup,
    NoBarsInRange,
    CalculationFailure,
    EngineFailure,
    Cancelled
};

struct BacktestError {
    BacktestErrorCode code{};
    std::string path;
    std::string message;
};

struct BacktestOutcome {
    BacktestRequest request;
    BacktestStatus status{BacktestStatus::Preparing};
    BacktestResultStatus result_status{BacktestResultStatus::Failed};
    std::vector<BacktestStatus> status_history{BacktestStatus::Preparing};
    std::optional<Market::Core::Time::UtcTimestamp> warmup_begin;
    std::vector<BacktestInput> inputs;
    std::vector<ResolutionError> input_errors;
    std::vector<BacktestError> errors;
    std::vector<StrategyEvent> audit_events;
    std::optional<RunResult> result;
};

[[nodiscard]] std::vector<ValidationError> validate(const BacktestRequest&, IndicatorCatalog = {});

class BacktestRunner {
    Analysis::Core::Indicator::Analyzer* m_analyzer;

public:
    explicit BacktestRunner(Analysis::Core::Indicator::Analyzer& analyzer) : m_analyzer(&analyzer) {}

    [[nodiscard]] BacktestOutcome run(const BacktestRequest&, Market::Core::Provider::Data&,
                                      std::span<const Market::Core::Time::Frame> supported_timeframes,
                                      std::function<bool()> cancelled = {}) const;
};
} // namespace Didrachma::Strategy::Core
