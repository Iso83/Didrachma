#pragma once

#include <Didrachma/analysis/core/indicator/Parameter.h>
#include <Didrachma/market/core/time/Frame.h>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Didrachma::Strategy::Core {
using Duration = std::chrono::seconds;

enum class Direction { Long, Short };
enum class InstrumentKind { Subject, Fixed };

struct InstrumentSelector {
    InstrumentKind kind{InstrumentKind::Subject};
    std::string symbol;
};

struct SeriesBinding {
    std::string id;
    std::string provider_id;
    InstrumentSelector instrument;
    Market::Core::Time::Frame timeframe;
    std::optional<Duration> maximum_data_age;
};

struct IndicatorBinding {
    std::string id;
    std::string series_id;
    std::string definition_id;
    std::map<std::string, Analysis::Core::Indicator::ParameterValue> parameters;
};

enum class Comparison { Less, LessOrEqual, Equal, GreaterOrEqual, Greater };
enum class MarketField { Open, High, Low, Close, Volume };
enum class CrossDirection { Above, Below, Either };
enum class ReturnKind { Gain, Loss };
enum class ConditionKind {
    MarketComparison,
    IndicatorComparison,
    IndicatorCross,
    PatternOccurrence,
    ElapsedTime,
    ClosedBarCount,
    UnrealizedReturn,
    All,
    Any,
    Not,
    Sequence
};

struct MarketComparison {
    std::string series_id;
    MarketField field{MarketField::Close};
    Comparison comparison{Comparison::Greater};
    double value{};
};

struct IndicatorComparison {
    std::string indicator_id;
    std::string output_id;
    Comparison comparison{Comparison::Greater};
    double value{};
};

struct IndicatorCross {
    std::string left_indicator_id;
    std::string left_output_id;
    std::optional<std::string> right_indicator_id;
    std::string right_output_id;
    std::optional<double> constant;
    CrossDirection direction{CrossDirection::Either};
};

struct PatternOccurrence {
    std::string indicator_id;
    std::optional<Direction> direction;
};

struct ElapsedTimeCondition {
    Comparison comparison{Comparison::GreaterOrEqual};
    Duration duration{};
};

struct ClosedBarCountCondition {
    Comparison comparison{Comparison::GreaterOrEqual};
    std::uint64_t count{};
};

struct UnrealizedReturnCondition {
    ReturnKind kind{ReturnKind::Gain};
    Comparison comparison{Comparison::GreaterOrEqual};
    double percentage{};
};

struct SequenceLimits {
    std::optional<Duration> maximum_elapsed;
    std::optional<std::uint64_t> maximum_closed_bars;
};

struct ConditionExpression {
    std::string id;
    ConditionKind kind{ConditionKind::All};
    std::variant<std::monostate, MarketComparison, IndicatorComparison, IndicatorCross, PatternOccurrence,
                 ElapsedTimeCondition, ClosedBarCountCondition, UnrealizedReturnCondition>
        predicate;
    std::vector<std::shared_ptr<ConditionExpression>> children;
    SequenceLimits sequence;
};

enum class PricePolicyKind { Absolute, PercentageFromEntry, IndicatorValue };

struct PricePolicy {
    PricePolicyKind kind{PricePolicyKind::Absolute};
    double value{};
    std::string indicator_id;
    std::string output_id;
    double offset{};
};

enum class EntryOrderKind { NextBarOpen, Limit };

struct EntryOrder {
    EntryOrderKind kind{EntryOrderKind::NextBarOpen};
    double limit_price{};
    std::uint64_t validity_primary_bars{};
};

struct EntryPlan {
    std::shared_ptr<ConditionExpression> condition;
    EntryOrder order;
};

struct ExitCondition {
    std::string id;
    std::shared_ptr<ConditionExpression> condition;
};

enum class ActionKind { AdjustStop, AdjustTarget, Exit };

struct RuntimeAction {
    ActionKind kind{ActionKind::Exit};
    std::optional<PricePolicy> price;
    std::string exit_reason;
};

struct RuntimeRule {
    std::string id;
    std::int32_t priority{};
    std::shared_ptr<ConditionExpression> condition;
    std::vector<RuntimeAction> actions;
};

struct Definition {
    std::string id;
    std::string display_name;
    std::uint32_t version{1};
    std::string description;
    Direction direction{Direction::Long};
    std::string primary_series_id;
    std::vector<SeriesBinding> series;
    std::vector<IndicatorBinding> indicators;
    EntryPlan entry;
    PricePolicy stop_loss;
    PricePolicy target;
    std::vector<ExitCondition> exits;
    std::vector<RuntimeRule> runtime_rules;
};

class Snapshot {
    std::shared_ptr<const Definition> m_definition;

public:
    explicit Snapshot(const Definition& definition);

    [[nodiscard]] const Definition& definition() const {
        return *m_definition;
    }
};
} // namespace Didrachma::Strategy::Core
