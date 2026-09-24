#pragma once
#include <Didrachma/analysis/core/indicator/Definition.h>
#include <Didrachma/strategy/core/Model.h>

namespace Didrachma::Strategy::Core::Testing {
inline std::shared_ptr<ConditionExpression> leaf(std::string id, ConditionKind kind, auto predicate) {
    auto value = std::make_shared<ConditionExpression>();
    value->id = std::move(id);
    value->kind = kind;
    value->predicate = std::move(predicate);
    return value;
}

inline std::shared_ptr<ConditionExpression> group(std::string id, ConditionKind kind,
                                                  std::vector<std::shared_ptr<ConditionExpression>> children) {
    auto value = std::make_shared<ConditionExpression>();
    value->id = std::move(id);
    value->kind = kind;
    value->children = std::move(children);
    return value;
}

inline Definition representative() {
    Definition value{"momentum-peer",
                     "Momentum with sector confirmation",
                     3,
                     "Enter after intraday momentum and daily peer confirmation.",
                     Direction::Long,
                     "subject-10m"};
    value.series = {
        {"subject-10m",
         "fixture",
         {InstrumentKind::Subject, ""},
         {10, Market::Core::Time::Unit::Minute},
         Duration{900}},
        {"subject-1h", "fixture", {InstrumentKind::Subject, ""}, {1, Market::Core::Time::Unit::Hour}, Duration{7200}},
        {"sector-daily",
         "fixture",
         {InstrumentKind::Fixed, "XLK"},
         {1, Market::Core::Time::Unit::Day},
         Duration{172800}}};
    value.indicators = {{"fast", "subject-10m", "sma", {{"period", std::int64_t{10}}}},
                        {"slow", "subject-1h", "sma", {{"period", std::int64_t{20}}}},
                        {"peer-pattern", "sector-daily", "cdldoji", {}}};
    auto cross = leaf("fast-cross", ConditionKind::IndicatorCross,
                      IndicatorCross{"fast", "value", "slow", "value", {}, CrossDirection::Above});
    auto pattern =
        leaf("peer-doji", ConditionKind::PatternOccurrence, PatternOccurrence{"peer-pattern", Direction::Long});
    auto volume = leaf("volume", ConditionKind::MarketComparison,
                       MarketComparison{"subject-10m", MarketField::Volume, Comparison::Greater, 1000});
    auto sequence = group("setup-sequence", ConditionKind::Sequence, {pattern, cross});
    sequence->sequence = {Duration{7200}, 12};
    value.entry.condition =
        group("entry-all", ConditionKind::All,
              {sequence, group("liquidity-or", ConditionKind::Any,
                               {volume, group("not-too-late", ConditionKind::Not,
                                              {leaf("bars", ConditionKind::ClosedBarCount,
                                                    ClosedBarCountCondition{Comparison::Greater, 50})})})});
    value.entry.order = {EntryOrderKind::Limit, 100.0, 3};
    value.stop_loss = {PricePolicyKind::PercentageFromEntry, 2.0};
    value.target = {PricePolicyKind::PercentageFromEntry, 5.0};
    value.exits = {{"timed-exit", leaf("elapsed", ConditionKind::ElapsedTime,
                                       ElapsedTimeCondition{Comparison::GreaterOrEqual, Duration{86400}})}};
    value.runtime_rules = {{"protect-profit",
                            10,
                            leaf("gain", ConditionKind::UnrealizedReturn,
                                 UnrealizedReturnCondition{ReturnKind::Gain, Comparison::GreaterOrEqual, 3.0}),
                            {{ActionKind::AdjustStop, PricePolicy{PricePolicyKind::PercentageFromEntry, 1.0}, ""}}},
                           {"risk-exit",
                            20,
                            leaf("loss", ConditionKind::UnrealizedReturn,
                                 UnrealizedReturnCondition{ReturnKind::Loss, Comparison::GreaterOrEqual, 4.0}),
                            {{ActionKind::Exit, {}, "maximum-loss"}}}};
    return value;
}

inline std::vector<Analysis::Core::Indicator::Definition> catalog() {
    using namespace Analysis::Core::Indicator;
    return {
        {"sma", "SMA", {{"period", "Period", ParameterKind::Integer, std::int64_t{20}, 1, 500}}, {{"value", "Value"}}},
        {"cdldoji", "Doji", {}, {{"value", "Pattern", VisualKind::Marker}}}};
}
} // namespace Didrachma::Strategy::Core::Testing
