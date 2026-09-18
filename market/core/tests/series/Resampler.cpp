#include "TestAssert.h"

#include <Didrachma/market/core/series/Resampler.h>

using namespace Didrachma::Market::Core;
using Timestamp = Time::UtcTimestamp;

Series::Bar bar(int minute, double open, double close, Series::BarState state = Series::BarState::Closed) {
    const auto time = Timestamp{std::chrono::minutes{minute}};
    return {time, time + std::chrono::minutes{10}, open, std::max(open, close), std::min(open, close), close, 2, state};
}

int test_utc_buckets_gaps_and_out_of_order_input() {
    const std::vector input{bar(50, 3, 4), bar(0, 1, 2), bar(20, 2, 3)};
    const auto result = Series::resample(input, {10, Time::Unit::Minute}, {1, Time::Unit::Hour});
    CPPTEST_ASSERT(result.error.empty() && result.bars.size() == 1);
    CPPTEST_ASSERT(result.bars[0].open_time == Timestamp{} &&
                   result.bars[0].close_time == Timestamp{std::chrono::hours{1}});
    CPPTEST_ASSERT(result.bars[0].open == 1 && result.bars[0].close == 4 && result.bars[0].volume == 6);
    return 0;
}

int test_session_boundary_and_forming_state() {
    const std::vector input{bar(23 * 60 + 50, 1, 2), bar(24 * 60, 2, 3, Series::BarState::Forming)};
    const auto result = Series::resample(input, {10, Time::Unit::Minute}, {1, Time::Unit::Day});
    CPPTEST_ASSERT(result.bars.size() == 2);
    CPPTEST_ASSERT(result.bars[0].state == Series::BarState::Closed);
    CPPTEST_ASSERT(result.bars[1].open_time == Timestamp{std::chrono::days{1}});
    CPPTEST_ASSERT(result.bars[1].state == Series::BarState::Forming);
    CPPTEST_ASSERT(Series::resample(input, {1, Time::Unit::Hour}, {10, Time::Unit::Minute}).error.size() > 0);
    return 0;
}

int test_five_minute_bars_resample_by_timestamp_to_ten_minutes() {
    const auto make = [](int minute, double open, double high, double low, double close, double volume) {
        const auto time = Timestamp{std::chrono::minutes{minute}};
        return Series::Bar{time,   time + std::chrono::minutes{5}, open, high, low, close,
                           volume, Series::BarState::Closed};
    };
    const std::vector input{make(30, 100, 103, 99, 102, 1000), make(35, 102, 106, 101, 105, 1500),
                            make(50, 105, 108, 104, 107, 500)};
    const auto result = Series::resample(input, {5, Time::Unit::Minute}, {10, Time::Unit::Minute});
    CPPTEST_ASSERT(result.error.empty() && result.bars.size() == 2);
    CPPTEST_ASSERT(result.bars[0].open_time == Timestamp{std::chrono::minutes{30}});
    CPPTEST_ASSERT(result.bars[0].open == 100 && result.bars[0].high == 106 && result.bars[0].low == 99 &&
                   result.bars[0].close == 105 && result.bars[0].volume == 2500);
    CPPTEST_ASSERT(result.bars[1].open_time == Timestamp{std::chrono::minutes{50}});
    return 0;
}

int main() {
    CPPTEST_RUN(test_utc_buckets_gaps_and_out_of_order_input);
    CPPTEST_RUN(test_session_boundary_and_forming_state);
    CPPTEST_RUN(test_five_minute_bars_resample_by_timestamp_to_ten_minutes);
    return 0;
}
