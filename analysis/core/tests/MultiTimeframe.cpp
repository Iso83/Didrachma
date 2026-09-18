#include "TestAssert.h"

#include <Didrachma/analysis/core/multiTimeframe/Analysis.h>

using namespace Didrachma;
using namespace Didrachma::Analysis::Core::MultiTimeframe;
using Timestamp = Market::Core::Time::UtcTimestamp;

Timestamp at(int seconds) {
    return Timestamp{std::chrono::seconds{seconds}};
}

int test_closed_alignment_has_no_look_ahead() {
    const std::vector values{TimedValue{at(0), at(60), 1, true}, TimedValue{at(60), at(120), 2, true},
                             TimedValue{at(120), at(180), 3, false}};
    CPPTEST_ASSERT(align_closed(values, at(59)) == nullptr);
    CPPTEST_ASSERT(align_closed(values, at(60))->value == 1);
    CPPTEST_ASSERT(align_closed(values, at(119))->value == 1);
    CPPTEST_ASSERT(align_closed(values, at(120))->value == 2);
    CPPTEST_ASSERT(align_closed(values, at(999))->value == 2);
    return 0;
}

int test_graph_diagnostics_order_and_dirty_tail() {
    DependencyGraph graph;
    CPPTEST_ASSERT(graph.add({"10m", NodeKind::Series, {}}));
    CPPTEST_ASSERT(graph.add({"1h", NodeKind::Indicator, {"10m"}, std::chrono::hours{1}}));
    CPPTEST_ASSERT(graph.add({"trend", NodeKind::Condition, {"1h"}, std::chrono::hours{2}}));
    CPPTEST_ASSERT(graph.validate().empty());
    const auto order = graph.order();
    CPPTEST_ASSERT(order.front() == "10m" && order.back() == "trend");
    const auto dirty = graph.propagate("10m", {at(3600), at(4200)});
    CPPTEST_ASSERT(dirty.at("1h").begin == at(0));
    CPPTEST_ASSERT(dirty.at("trend").begin == at(-7200));

    DependencyGraph missing;
    missing.add({"condition", NodeKind::Condition, {"absent"}});
    CPPTEST_ASSERT(missing.validate()[0].message.find("missing") != std::string::npos);
    DependencyGraph cycle;
    cycle.add({"a", NodeKind::Indicator, {"b"}});
    cycle.add({"b", NodeKind::Indicator, {"a"}});
    CPPTEST_ASSERT(!cycle.validate().empty() && cycle.order().empty());
    return 0;
}

int test_three_timeframe_condition_and_forming_suppression() {
    const InputBinding ten{"tenMinute", InputKind::Series, {10, Market::Core::Time::Unit::Minute}, "close"};
    const InputBinding hour{"hour", InputKind::Indicator, {1, Market::Core::Time::Unit::Hour}, "sma"};
    const InputBinding day{"day", InputKind::Indicator, {1, Market::Core::Time::Unit::Day}, "sma"};
    const std::vector ten_values{TimedValue{at(10), at(20), 1}, TimedValue{at(20), at(30), 2}};
    const std::vector hour_values{TimedValue{at(0), at(10), 10}, TimedValue{at(10), at(25), 11}};
    const std::vector day_values{TimedValue{at(-20), at(-10), 100}, TimedValue{at(-10), at(30), 101}};
    const std::vector inputs{BoundInput{ten, ten_values}, BoundInput{hour, hour_values}, BoundInput{day, day_values}};
    const std::vector times{at(29), at(30)};
    const Market::Core::Series::Key key{"fixture", "ABC", {10, Market::Core::Time::Unit::Minute}};
    const auto events = TrendInterest{}.evaluate(key, inputs, times);
    CPPTEST_ASSERT(events.size() == 1 && events[0].start == at(30));
    CPPTEST_ASSERT(events[0].evidence.size() == 3 && events[0].evidence.at("day") == 101);

    auto forming_days = day_values;
    forming_days.back().closed = false;
    const std::vector forming_inputs{BoundInput{ten, ten_values}, BoundInput{hour, hour_values},
                                     BoundInput{day, forming_days}};
    CPPTEST_ASSERT(TrendInterest{}.evaluate(key, forming_inputs, times).empty());
    return 0;
}

int main() {
    CPPTEST_RUN(test_closed_alignment_has_no_look_ahead);
    CPPTEST_RUN(test_graph_diagnostics_order_and_dirty_tail);
    CPPTEST_RUN(test_three_timeframe_condition_and_forming_suppression);
    return 0;
}
