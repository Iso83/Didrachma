#include "Fixture.h"
#include "TestAssert.h"

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

int main() {
    CPPTEST_RUN(test_model_covers_subject_fixed_multitimeframe_and_ordered_sequence);
    CPPTEST_RUN(test_snapshot_is_deep_and_immutable_from_definition_edits);
    return 0;
}
