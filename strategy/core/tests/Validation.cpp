#include "Fixture.h"
#include "TestAssert.h"

#include <Didrachma/strategy/core/Backtest.h>
#include <algorithm>

using namespace Didrachma::Strategy::Core;

bool has(const std::vector<ValidationError>& errors, ValidationCode code) {
    return std::ranges::any_of(errors, [&](const auto& value) { return value.code == code; });
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
    request.fill_model_version = 0;
    const auto errors = validate(request, Testing::catalog());
    CPPTEST_ASSERT(has(errors, ValidationCode::InvalidBacktestRequest));
    CPPTEST_ASSERT(errors.size() >= 4);
    return 0;
}

int main() {
    CPPTEST_RUN(test_representative_definition_is_valid);
    CPPTEST_RUN(test_duplicate_missing_timeframe_instrument_and_parameters);
    CPPTEST_RUN(test_cycles_prices_conditions_and_actions_are_rejected);
    CPPTEST_RUN(test_sequence_timeout_and_out_of_order_are_explicit_model_state);
    CPPTEST_RUN(test_entry_order_validation);
    CPPTEST_RUN(test_backtest_request_validation);
    return 0;
}
