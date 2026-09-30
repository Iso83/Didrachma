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
    std::vector<Market::Core::Series::Bar> bars;
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

struct BacktestTimings {
    std::optional<std::chrono::nanoseconds> data_preparation;
    std::optional<std::chrono::nanoseconds> indicator_calculation;
    std::optional<std::chrono::nanoseconds> strategy_execution;
    std::optional<std::chrono::nanoseconds> total;
};

struct BacktestOccurrence {
    std::uint64_t id{};
    BacktestResultStatus status{BacktestResultStatus::Failed};
    RunResult result;
};

struct BacktestAuditEvent {
    std::optional<std::uint64_t> occurrence_id;
    StrategyEvent event;
};

struct BacktestEvaluation {
    std::optional<std::uint64_t> occurrence_id;
    Evaluation evaluation;
};

struct BacktestSummary {
    std::uint64_t signal_count{};
    std::uint64_t filled_count{};
    std::uint64_t closed_count{};
    std::uint64_t target_count{};
    std::uint64_t stop_count{};
    std::uint64_t end_of_range_count{};
    double gross_profit_loss{};
    double costs{};
    double net_profit_loss{};
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
    std::vector<BacktestAuditEvent> audit_events;
    std::vector<BacktestEvaluation> evaluations;
    std::vector<BacktestOccurrence> occurrences;
    BacktestSummary summary;
    BacktestTimings timings;
};

enum class BacktestProgressKind { Preparing, Loading, Ready, Executing, Completed, Failed, Cancelled };

struct BacktestProgress {
    BacktestProgressKind kind{BacktestProgressKind::Preparing};
    std::string binding_id;
    std::optional<BindingState> binding_state;
};

using BacktestProgressObserver = std::function<void(const BacktestProgress&)>;

// Provider metadata used by the pure, no-I/O planning boundary.  A requested
// frame may map to a different native frame (for example Yahoo 10m -> 5m).
struct BacktestSourceCapability {
    Market::Core::Time::Frame requested;
    Market::Core::Time::Frame source;
    std::optional<std::chrono::days> history_reach;
};

struct BacktestPlannedInput {
    std::string binding_id;
    std::string display_name;
    Market::Core::Series::Key resolved_key;
    Market::Core::Series::Key provider_request_key;
    Market::Core::Time::Frame requested;
    Market::Core::Time::Frame source;
    bool derived{};
    std::size_t required_history{};
    Market::Core::Time::UtcTimestamp evaluation_from;
    Market::Core::Time::UtcTimestamp evaluation_through;
    Market::Core::Time::UtcTimestamp load_begin;
    Market::Core::Time::UtcTimestamp exclusive_end;
    std::optional<std::chrono::days> history_reach;
    std::vector<BacktestError> errors;
};

struct BacktestPlannedLoad {
    Market::Core::Series::Key request_key;
    Market::Core::Time::Frame transport_source;
    std::vector<std::string> binding_ids;
    std::vector<std::string> dependent_binding_ids;
    std::size_t required_history{};
    Market::Core::Time::UtcTimestamp load_begin;
    Market::Core::Time::UtcTimestamp exclusive_end;
    std::optional<std::chrono::days> history_reach;
};

struct BacktestPlan {
    std::vector<BacktestPlannedInput> inputs;
    std::vector<BacktestPlannedLoad> loads;
    std::vector<std::pair<std::string, std::string>> derivations;
    std::vector<ResolutionError> resolution_errors;
    std::vector<BacktestError> errors;
};

[[nodiscard]] Market::Core::Time::UtcTimestamp
exclusive_history_end(Market::Core::Time::UtcTimestamp inclusive_through);
[[nodiscard]] BacktestPlan plan_backtest(const BacktestRequest&, Analysis::Core::Indicator::Analyzer&,
                                         std::span<const BacktestSourceCapability>,
                                         Market::Core::Time::UtcTimestamp now);

[[nodiscard]] std::vector<ValidationError> validate(const BacktestRequest&, IndicatorCatalog = {});

class BacktestRunner {
    Analysis::Core::Indicator::Analyzer* m_analyzer;

public:
    explicit BacktestRunner(Analysis::Core::Indicator::Analyzer& analyzer) : m_analyzer(&analyzer) {}

    [[nodiscard]] BacktestOutcome run(const BacktestRequest&, Market::Core::Provider::Data&,
                                      std::span<const Market::Core::Time::Frame> supported_timeframes,
                                      std::function<bool()> cancelled = {},
                                      BacktestProgressObserver progress = {}) const;
    [[nodiscard]] BacktestOutcome run(const BacktestRequest&, Market::Core::Provider::Data&,
                                      std::span<const BacktestSourceCapability>, Market::Core::Time::UtcTimestamp now,
                                      std::function<bool()> cancelled = {},
                                      BacktestProgressObserver progress = {}) const;
};
} // namespace Didrachma::Strategy::Core
