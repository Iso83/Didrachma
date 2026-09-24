#pragma once

#include <Didrachma/strategy/core/Evaluation.h>

namespace Didrachma::Strategy::Core {
enum class RunState { WaitingForEntry, EntryArmed, Running, Exited, Stopped, Error };
enum class StrategyEventKind {
    EntryArmed,
    EntryFilled,
    EntryExpired,
    StopAdjusted,
    TargetAdjusted,
    ExitArmed,
    ExitTriggered,
    RunStopped,
    Error
};
// Separates an absent signal from a signal that could not yet (or no longer can)
// fill within its persisted order policy.
enum class EntryStatus { NoSignal, AwaitingFill, Expired, Filled };
enum class ExitReason { None, Condition, RuntimeRule, StopLoss, Target, UserStop, EndOfRange, Error };
enum class ProjectionKind { EntryMarker, ExitMarker, EntryPrice, StopPrice, TargetPrice };

struct ExecutionCosts {
    double quantity{1.0};
    std::optional<double> starting_capital;
    double fixed_per_fill{};
    double percentage_per_fill{};
    double slippage_percentage{};
};

struct StrategyEvent {
    StrategyEventKind kind{};
    Market::Core::Time::UtcTimestamp evaluation_time;
    Market::Core::Time::UtcTimestamp effective_time;
    RunState old_state{};
    RunState new_state{};
    std::vector<std::string> trigger_ids;
    std::optional<double> old_value;
    std::optional<double> new_value;
    std::vector<Evidence> evidence;
    std::string detail;
};

struct PriceSegment {
    ProjectionKind kind{};
    Market::Core::Time::UtcTimestamp begin;
    Market::Core::Time::UtcTimestamp end;
    double price{};
};

struct Projection {
    struct Marker {
        ProjectionKind kind{};
        Market::Core::Time::UtcTimestamp time;
        double price{};
    };
    std::vector<Marker> markers;
    std::vector<PriceSegment> segments;
};

struct RunResult {
    RunState state{RunState::WaitingForEntry};
    EntryStatus entry_status{EntryStatus::NoSignal};
    ExitReason exit_reason{ExitReason::None};
    std::optional<Market::Core::Time::UtcTimestamp> entry_time;
    std::optional<Market::Core::Time::UtcTimestamp> exit_time;
    std::optional<double> entry_price;
    std::optional<double> exit_price;
    std::optional<double> current_price;
    double unrealized_profit_loss{};
    double unrealized_return_percentage{};
    double gross_profit_loss{};
    double net_profit_loss{};
    double return_percentage{};
    Duration duration{};
    Duration elapsed{};
    double maximum_favorable_excursion{};
    double maximum_adverse_excursion{};
    std::uint64_t closed_bar_count{};
    std::map<std::string, std::uint64_t> trigger_counts;
    std::vector<StrategyEvent> events;
    Projection projection;
    // Same-bar OHLC ordering is unknowable. The engine ignores a target-only
    // touch after an intrabar limit fill and uses the stop when adverse and
    // favorable levels both may have followed that fill.
    bool ambiguous_fill{};
    std::vector<std::string> warnings;
};

class Engine {
    struct Implementation;
    std::unique_ptr<Implementation> m_implementation;

public:
    Engine(Snapshot, DataGraph&, ExecutionCosts = {});
    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] RunState state() const;
    [[nodiscard]] const RunResult& result() const;
    void process(const Market::Core::Series::Bar&);
    void stop(Market::Core::Time::UtcTimestamp, std::string detail = "user stop");
    [[nodiscard]] RunResult finish();
    [[nodiscard]] RunResult replay();
};
} // namespace Didrachma::Strategy::Core
