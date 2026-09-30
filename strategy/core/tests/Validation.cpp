#include "Fixture.h"
#include "TestAssert.h"

#include <Didrachma/strategy/core/Backtest.h>
#include <algorithm>
#include <limits>

using namespace Didrachma::Strategy::Core;

bool has(const std::vector<ValidationError>& errors, ValidationCode code) {
    return std::ranges::any_of(errors, [&](const auto& value) { return value.code == code; });
}

bool has_path(const std::vector<ValidationError>& errors, std::string_view path) {
    return std::ranges::any_of(errors, [&](const auto& value) { return value.path == path; });
}

int test_representative_definition_is_valid() {
    CPPTEST_ASSERT(validate(Testing::representative(), Testing::catalog()).empty());
    return 0;
}

int test_duplicate_missing_timeframe_instrument_and_parameters() {
    auto value = Testing::representative();
    value.series[1].id = value.series[0].id;
    value.primary_series_id = "missing";
    value.series[0].timeframe.quantity = 0;
    value.series[2].instrument.symbol = "";
    value.indicators[0].parameters["period"] = std::int64_t{0};
    const auto errors = validate(value, Testing::catalog());
    CPPTEST_ASSERT(has(errors, ValidationCode::DuplicateId));
    CPPTEST_ASSERT(has(errors, ValidationCode::MissingReference));
    CPPTEST_ASSERT(has(errors, ValidationCode::InvalidTimeframe));
    CPPTEST_ASSERT(has(errors, ValidationCode::InvalidInstrument));
    CPPTEST_ASSERT(has(errors, ValidationCode::InvalidIndicatorParameter));
    return 0;
}

int test_cycles_prices_conditions_and_actions_are_rejected() {
    auto value = Testing::representative();
    auto cycle = Testing::group("cycle", ConditionKind::All, {});
    cycle->children.push_back(cycle);
    value.entry.condition = cycle;
    value.stop_loss = {PricePolicyKind::Absolute, -1};
    value.target = {PricePolicyKind::IndicatorValue, 0, "missing", "value"};
    value.runtime_rules[0].actions[0].price.reset();
    value.runtime_rules[1].actions[0].exit_reason = "";
    const auto errors = validate(value, Testing::catalog());
    CPPTEST_ASSERT(has(errors, ValidationCode::DependencyCycle));
    CPPTEST_ASSERT(has(errors, ValidationCode::InvalidPricePolicy));
    CPPTEST_ASSERT(has(errors, ValidationCode::MissingReference));
    CPPTEST_ASSERT(has(errors, ValidationCode::InvalidAction));
    CPPTEST_ASSERT(has_path(errors, "/target/indicatorId"));
    return 0;
}

int test_repairable_condition_fields_have_exact_paths() {
    auto value = Testing::representative();
    auto& cross = std::get<IndicatorCross>(value.entry.condition->children[0]->children[1]->predicate);
    cross.left_output_id = "missing";
    auto& market = std::get<MarketComparison>(value.entry.condition->children[1]->children[0]->predicate);
    market.value = std::numeric_limits<double>::infinity();
    const auto errors = validate(value, Testing::catalog());
    CPPTEST_ASSERT(has_path(errors, "/entry/condition/children/0/children/1/predicate/leftOutputId"));
    CPPTEST_ASSERT(has_path(errors, "/entry/condition/children/1/children/0/predicate/value"));
    return 0;
}

int test_editor_repair_controls_have_exact_validation_paths() {
    bool paths_valid = true;
    auto validate_path = [&](auto mutate, std::string_view path) {
        auto value = Testing::representative();
        mutate(value);
        paths_valid = paths_valid && has_path(validate(value, Testing::catalog()), path);
    };

    validate_path([](Definition& value) { value.series[2].instrument.symbol.clear(); }, "/series/2/instrument/symbol");
    validate_path([](Definition& value) { value.indicators[0].parameters["period"] = std::int64_t{0}; },
                  "/indicators/0/parameters/period");
    validate_path(
        [](Definition& value) {
            value.entry.condition = Testing::leaf("comparison", ConditionKind::IndicatorComparison,
                                                  IndicatorComparison{"missing", "value", Comparison::Greater, 1});
        },
        "/entry/condition/predicate/indicatorId");
    validate_path(
        [](Definition& value) {
            value.entry.condition = Testing::leaf("comparison", ConditionKind::IndicatorComparison,
                                                  IndicatorComparison{"fast", "missing", Comparison::Greater, 1});
        },
        "/entry/condition/predicate/outputId");
    validate_path(
        [](Definition& value) {
            value.entry.condition =
                Testing::leaf("cross", ConditionKind::IndicatorCross,
                              IndicatorCross{"missing", "value", "slow", "value", {}, CrossDirection::Above});
        },
        "/entry/condition/predicate/leftIndicatorId");
    validate_path(
        [](Definition& value) {
            value.entry.condition =
                Testing::leaf("cross", ConditionKind::IndicatorCross,
                              IndicatorCross{"fast", "missing", "slow", "value", {}, CrossDirection::Above});
        },
        "/entry/condition/predicate/leftOutputId");
    validate_path(
        [](Definition& value) {
            value.entry.condition =
                Testing::leaf("cross", ConditionKind::IndicatorCross,
                              IndicatorCross{"fast", "value", "missing", "value", {}, CrossDirection::Above});
        },
        "/entry/condition/predicate/rightIndicatorId");
    validate_path(
        [](Definition& value) {
            value.entry.condition =
                Testing::leaf("cross", ConditionKind::IndicatorCross,
                              IndicatorCross{"fast", "value", "slow", "missing", {}, CrossDirection::Above});
        },
        "/entry/condition/predicate/rightOutputId");
    validate_path([](Definition& value) { value.target = {PricePolicyKind::IndicatorValue, 0, "missing", "value"}; },
                  "/target/indicatorId");
    validate_path([](Definition& value) { value.target = {PricePolicyKind::IndicatorValue, 0, "fast", "missing"}; },
                  "/target/outputId");
    validate_path([](Definition& value) { value.entry.condition = Testing::group("empty", ConditionKind::All, {}); },
                  "/entry/condition/children");
    validate_path(
        [](Definition& value) {
            auto sequence =
                Testing::group("sequence", ConditionKind::Sequence,
                               {Testing::leaf("leaf", ConditionKind::ClosedBarCount, ClosedBarCountCondition{})});
            sequence->sequence.maximum_elapsed = Duration::zero();
            value.entry.condition = sequence;
        },
        "/entry/condition/sequence/maximumElapsedSeconds");
    validate_path(
        [](Definition& value) {
            auto sequence =
                Testing::group("sequence", ConditionKind::Sequence,
                               {Testing::leaf("leaf", ConditionKind::ClosedBarCount, ClosedBarCountCondition{})});
            sequence->sequence.maximum_closed_bars = 0;
            value.entry.condition = sequence;
        },
        "/entry/condition/sequence/maximumClosedBars");
    validate_path([](Definition& value) { value.runtime_rules[1].actions[0].exit_reason.clear(); },
                  "/runtimeRules/1/actions/0/exitReason");
    validate_path(
        [](Definition& value) { value.runtime_rules[1].actions[0].price = PricePolicy{PricePolicyKind::Absolute, 1}; },
        "/runtimeRules/1/actions/0/price");
    validate_path([](Definition& value) { value.runtime_rules[0].actions[0].price.reset(); },
                  "/runtimeRules/0/actions/0/price");
    validate_path([](Definition& value) { value.entry.order.limit_price = 0; }, "/entry/order/limitPrice");
    validate_path([](Definition& value) { value.entry.order.validity_primary_bars = 0; },
                  "/entry/order/validityPrimaryBars");
    CPPTEST_ASSERT(paths_valid);
    return 0;
}

int test_sequence_timeout_and_out_of_order_are_explicit_model_state() {
    auto value = Testing::representative();
    auto sequence = value.entry.condition->children[0];
    CPPTEST_ASSERT(sequence->children[0]->id == "peer-doji" && sequence->children[1]->id == "fast-cross");
    std::swap(sequence->children[0], sequence->children[1]);
    CPPTEST_ASSERT(sequence->children[0]->id == "fast-cross");
    sequence->sequence.maximum_closed_bars = 0;
    CPPTEST_ASSERT(has(validate(value, Testing::catalog()), ValidationCode::InvalidCondition));
    return 0;
}

int test_entry_order_validation() {
    auto value = Testing::representative();
    value.entry.order = {EntryOrderKind::NextBarOpen};
    CPPTEST_ASSERT(validate(value, Testing::catalog()).empty());
    value.entry.order = {EntryOrderKind::Limit, 101.25, 3};
    CPPTEST_ASSERT(validate(value, Testing::catalog()).empty());
    value.entry.order.validity_primary_bars = 0;
    CPPTEST_ASSERT(has(validate(value, Testing::catalog()), ValidationCode::InvalidEntryOrder));
    value.entry.order = {EntryOrderKind::NextBarOpen, 100.0, 1};
    CPPTEST_ASSERT(has(validate(value, Testing::catalog()), ValidationCode::InvalidEntryOrder));
    return 0;
}

int test_backtest_request_validation() {
    BacktestRequest request{Testing::representative(),
                            "AAPL",
                            Didrachma::Market::Core::Time::UtcTimestamp{Duration{100}},
                            Didrachma::Market::Core::Time::UtcTimestamp{Duration{200}},
                            {"fixture", {}},
                            {},
                            1};
    CPPTEST_ASSERT(validate(request, Testing::catalog()).empty());
    request.subject_symbol = "";
    request.through = Didrachma::Market::Core::Time::UtcTimestamp{Duration{99}};
    request.execution.quantity = 0;
    request.fill_model_version = 2;
    const auto errors = validate(request, Testing::catalog());
    CPPTEST_ASSERT(has(errors, ValidationCode::InvalidBacktestRequest));
    CPPTEST_ASSERT(errors.size() >= 4);
    CPPTEST_ASSERT(std::ranges::any_of(errors, [](const auto& error) {
        return error.path == "/fillModelVersion" && error.message.find("only version 1") != std::string::npos;
    }));
    return 0;
}

int main() {
    CPPTEST_RUN(test_representative_definition_is_valid);
    CPPTEST_RUN(test_duplicate_missing_timeframe_instrument_and_parameters);
    CPPTEST_RUN(test_cycles_prices_conditions_and_actions_are_rejected);
    CPPTEST_RUN(test_sequence_timeout_and_out_of_order_are_explicit_model_state);
    CPPTEST_RUN(test_entry_order_validation);
    CPPTEST_RUN(test_repairable_condition_fields_have_exact_paths);
    CPPTEST_RUN(test_editor_repair_controls_have_exact_validation_paths);
    CPPTEST_RUN(test_backtest_request_validation);
    return 0;
}
