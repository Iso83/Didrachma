#include "TestAssert.h"

#include <Didrachma/analysis/core/condition/Pattern.h>

using namespace Didrachma;

namespace {
Market::Core::Time::UtcTimestamp at(int seconds) {
    return Market::Core::Time::UtcTimestamp{std::chrono::seconds{seconds}};
}

Analysis::Core::Indicator::Definition
definition(Analysis::Core::Indicator::EventDirection direction = Analysis::Core::Indicator::EventDirection::Signed) {
    using namespace Analysis::Core::Indicator;
    Definition value{"cdlfixture", "Fixture Pattern", {}, {{"value", "Integer", VisualKind::Marker}}};
    value.capability = Capability::AnalysisEvent;
    value.outputs.front().event_direction = direction;
    return value;
}
} // namespace

int test_closed_signed_zero_and_evidence() {
    using namespace Analysis::Core;
    const Market::Core::Series::Key key{"fixture", "ABC", {1, Market::Core::Time::Unit::Minute}};
    const Indicator::Instance instance{"pattern-1", "cdlfixture", true, {{"penetration", 0.3}}};
    std::vector<Market::Core::Series::Bar> bars{
        {at(10), at(11), 10, 12, 8, 11, 1, Market::Core::Series::BarState::Closed},
        {at(20), std::nullopt, 11, 13, 9, 10, 1, Market::Core::Series::BarState::Forming},
        {at(30), at(31), 10, 11, 7, 8, 1, Market::Core::Series::BarState::Closed}};
    const Indicator::Result result{
        {{"value", {{at(10), 100}, {at(20), -100}, {at(30), 0}}}}, 1, 1, Indicator::CalculationState::Ready};
    const auto first =
        Condition::extract_pattern_events(definition(), instance, result, bars, {"chart", "Pattern A", key});
    CPPTEST_ASSERT(first.size() == 1);
    CPPTEST_ASSERT(first[0].direction == Condition::Direction::Upward);
    CPPTEST_ASSERT(first[0].evidence.at("raw_pattern_value") == 100);
    CPPTEST_ASSERT(first[0].source_definition_id == "cdlfixture" && first[0].source_instance_id == "pattern-1");
    CPPTEST_ASSERT(first[0].source_name == "Pattern A" && first[0].source_parameters.at("penetration") == "0.300000");

    bars[1].state = Market::Core::Series::BarState::Closed;
    bars[1].close_time = at(21);
    const auto closed =
        Condition::extract_pattern_events(definition(), instance, result, bars, {"chart", "Pattern A", key});
    CPPTEST_ASSERT(closed.size() == 2 && closed[1].direction == Condition::Direction::Downward);
    CPPTEST_ASSERT(closed[0].id == first[0].id);
    return 0;
}

int test_neutral_and_recalculation_replacement_identity() {
    using namespace Analysis::Core;
    const Market::Core::Series::Key key{"fixture", "ABC", {1, Market::Core::Time::Unit::Day}};
    const Indicator::Instance instance{"pattern-2", "cdlfixture", true, {}};
    const std::vector<Market::Core::Series::Bar> bars{
        {at(10), at(11), 10, 12, 8, 11, 1, Market::Core::Series::BarState::Closed}};
    Indicator::Result result{{{"value", {{at(10), 1}}}}, 1, 1, Indicator::CalculationState::Ready};
    const auto initial = Condition::extract_pattern_events(definition(Indicator::EventDirection::Neutral), instance,
                                                           result, bars, {"chart", "Neutral", key});
    CPPTEST_ASSERT(initial.size() == 1 && initial[0].direction == Condition::Direction::Neutral);
    result.outputs[0].samples[0].value = 0;
    const auto corrected = Condition::extract_pattern_events(definition(Indicator::EventDirection::Neutral), instance,
                                                             result, bars, {"chart", "Neutral", key});
    CPPTEST_ASSERT(corrected.empty());
    result.outputs[0].samples[0].value = 1;
    const auto reset = Condition::extract_pattern_events(definition(Indicator::EventDirection::Neutral), instance,
                                                         result, bars, {"chart", "Neutral", key});
    CPPTEST_ASSERT(reset[0].id == initial[0].id);
    return 0;
}

int test_forming_replacement_close_and_next_forming_do_not_repaint() {
    using namespace Analysis::Core;
    const Market::Core::Series::Key key{"fixture", "ABC", {1, Market::Core::Time::Unit::Minute}};
    const Indicator::Instance instance{"pattern-live", "cdlfixture", true, {}};
    std::vector<Market::Core::Series::Bar> bars{
        {at(10), std::nullopt, 10, 12, 8, 11, 1, Market::Core::Series::BarState::Forming}};
    Indicator::Result result{{{"value", {{at(10), 100}}}}, 1, 1, Indicator::CalculationState::Ready};
    CPPTEST_ASSERT(
        Condition::extract_pattern_events(definition(), instance, result, bars, {"chart", "Live", key}).empty());
    bars[0].close = 9;
    result.outputs[0].samples[0].value = -100;
    CPPTEST_ASSERT(
        Condition::extract_pattern_events(definition(), instance, result, bars, {"chart", "Live", key}).empty());
    bars[0].state = Market::Core::Series::BarState::Closed;
    bars[0].close_time = at(20);
    const auto committed =
        Condition::extract_pattern_events(definition(), instance, result, bars, {"chart", "Live", key});
    CPPTEST_ASSERT(committed.size() == 1 && committed[0].direction == Condition::Direction::Downward);
    bars.push_back({at(20), std::nullopt, 9, 13, 8, 12, 1, Market::Core::Series::BarState::Forming});
    result.outputs[0].samples.push_back({at(20), 100});
    const auto next = Condition::extract_pattern_events(definition(), instance, result, bars, {"chart", "Live", key});
    CPPTEST_ASSERT(next.size() == 1 && next[0].id == committed[0].id &&
                   next[0].direction == Condition::Direction::Downward);
    return 0;
}

int main() {
    CPPTEST_RUN(test_closed_signed_zero_and_evidence);
    CPPTEST_RUN(test_neutral_and_recalculation_replacement_identity);
    CPPTEST_RUN(test_forming_replacement_close_and_next_forming_do_not_repaint);
    return 0;
}
