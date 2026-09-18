#include "TestAssert.h"

#include <Didrachma/analysis/core/condition/BandBreakout.h>
#include <Didrachma/analysis/core/condition/PriceCross.h>

using namespace Didrachma;
using namespace Didrachma::Analysis::Core;
using Timestamp = Market::Core::Time::UtcTimestamp;

Timestamp at(int value) {
    return Timestamp{std::chrono::seconds{value}};
}

Market::Core::Series::Bar bar(int time, double close,
                              Market::Core::Series::BarState state = Market::Core::Series::BarState::Closed) {
    return {at(time), at(time + 1), close, close, close, close, 1, state};
}

int test_exact_crosses_equality_warmup_gaps_and_forming_bars() {
    const Market::Core::Series::Key key{"test", "ABC", {1, Market::Core::Time::Unit::Day}};
    const std::vector bars{bar(1, 9), bar(2, 10), bar(4, 11), bar(5, 9),
                           bar(6, 12, Market::Core::Series::BarState::Forming)};
    const std::vector<Indicator::OutputSample> average{{at(1), 10}, {at(2), 10}, {at(4), 10}, {at(5), 10}, {at(6), 10}};
    const std::vector<Condition::NamedMarketInput> market{{"price", bars}};
    const std::vector<Condition::NamedIndicatorInput> indicators{{"ma50", average}};

    const auto events = Condition::PriceCross{}.evaluate({key, market, indicators});
    CPPTEST_ASSERT(events.size() == 2);
    CPPTEST_ASSERT(events[0].start == at(4) && events[0].direction == Condition::Direction::Upward);
    CPPTEST_ASSERT(events[1].start == at(5) && events[1].direction == Condition::Direction::Downward);
    CPPTEST_ASSERT(events[0].evidence.at("close") == 11 && events[0].evidence.at("previousMa50") == 10);
    CPPTEST_ASSERT(Condition::PriceCross{}.evaluate({key, market, indicators})[0].id == events[0].id);
    return 0;
}

int test_bollinger_breakout_transitions_and_evidence() {
    const Market::Core::Series::Key key{"test", "ABC", {1, Market::Core::Time::Unit::Day}};
    const std::vector bars{bar(1, 10), bar(2, 13), bar(3, 14), bar(4, 7),
                           bar(5, 6, Market::Core::Series::BarState::Forming)};
    const std::vector<Indicator::OutputSample> upper{{at(1), 12}, {at(2), 12}, {at(3), 12}, {at(4), 12}, {at(5), 12}};
    const std::vector<Indicator::OutputSample> lower{{at(1), 8}, {at(2), 8}, {at(3), 8}, {at(4), 8}, {at(5), 8}};
    const std::vector<Condition::NamedMarketInput> market{{"price", bars}};
    const std::vector<Condition::NamedIndicatorInput> indicators{{"upper", upper}, {"lower", lower}};

    const auto events = Condition::BandBreakout{}.evaluate({key, market, indicators});
    CPPTEST_ASSERT(events.size() == 2);
    CPPTEST_ASSERT(events[0].start == at(2) && events[0].direction == Condition::Direction::Upward);
    CPPTEST_ASSERT(events[1].start == at(4) && events[1].direction == Condition::Direction::Downward);
    CPPTEST_ASSERT(events[1].evidence.at("lower") == 8 && events[1].evidence.at("close") == 7);
    return 0;
}

int main() {
    CPPTEST_RUN(test_exact_crosses_equality_warmup_gaps_and_forming_bars);
    CPPTEST_RUN(test_bollinger_breakout_transitions_and_evidence);
    return 0;
}
