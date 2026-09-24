#include "Fixture.h"
#include "TestAssert.h"

#include <Didrachma/strategy/core/Backtest.h>

using namespace Didrachma::Strategy::Core;

int test_model_covers_subject_fixed_multitimeframe_and_ordered_sequence() {
    const auto value = Testing::representative();
    CPPTEST_ASSERT(value.series.size() == 3 && value.series[0].timeframe.quantity == 10);
    CPPTEST_ASSERT(value.series[2].instrument.kind == InstrumentKind::Fixed &&
                   value.series[2].instrument.symbol == "XLK");
    const auto sequence = value.entry.condition->children[0];
    CPPTEST_ASSERT(sequence->kind == ConditionKind::Sequence && sequence->children.size() == 2);
    CPPTEST_ASSERT(sequence->sequence.maximum_elapsed == Duration{7200} &&
                   sequence->sequence.maximum_closed_bars == 12);
    return 0;
}

int test_snapshot_is_deep_and_immutable_from_definition_edits() {
    auto value = Testing::representative();
    Snapshot snapshot(value);
    value.display_name = "Edited";
    value.entry.condition->children[0]->children[0]->id = "edited-step";
    CPPTEST_ASSERT(snapshot.definition().display_name == "Momentum with sector confirmation");
    CPPTEST_ASSERT(snapshot.definition().entry.condition->children[0]->children[0]->id == "peer-doji");
    return 0;
}

int test_backtest_contract_separates_reusable_and_resolved_state() {
    BacktestRequest request{Testing::representative(),
                            "AAPL",
                            Didrachma::Market::Core::Time::UtcTimestamp{Duration{100}},
                            Didrachma::Market::Core::Time::UtcTimestamp{Duration{200}},
                            {"fixture", {}},
                            {},
                            1};
    const auto resolution = resolve(request.strategy_snapshot, request.subject_symbol);
    CPPTEST_ASSERT(request.strategy_snapshot.series[0].instrument.symbol.empty());
    CPPTEST_ASSERT(resolution.errors.empty() && resolution.series[0].key.instrument == "AAPL");
    BacktestOutcome outcome{request};
    outcome.inputs.push_back({resolution.series[0], {Readiness::Ready, {}, 20, 20}});
    CPPTEST_ASSERT(outcome.request.subject_symbol == "AAPL" && outcome.inputs[0].state.readiness == Readiness::Ready);
    return 0;
}

int main() {
    CPPTEST_RUN(test_model_covers_subject_fixed_multitimeframe_and_ordered_sequence);
    CPPTEST_RUN(test_snapshot_is_deep_and_immutable_from_definition_edits);
    CPPTEST_RUN(test_backtest_contract_separates_reusable_and_resolved_state);
    return 0;
}
