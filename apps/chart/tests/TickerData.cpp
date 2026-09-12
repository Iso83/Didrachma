#include "data/TickerData.h"

#include "TestAssert.h"

using Didrachma::Apps::Chart::Data::TickerData;

int test_reserve() {
    TickerData data("TEST");
    data.reserve(4);

    CPPTEST_ASSERT(data.time.capacity() >= 4);
    CPPTEST_ASSERT(data.open.capacity() >= 4);
    CPPTEST_ASSERT(data.high.capacity() >= 4);
    CPPTEST_ASSERT(data.low.capacity() >= 4);
    CPPTEST_ASSERT(data.close.capacity() >= 4);
    CPPTEST_ASSERT(data.volume.capacity() >= 4);
    return 0;
}

int test_append() {
    TickerData data("TEST");
    data.push_back(100.0, 10.0, 12.0, 9.0, 11.0, 500.0);

    CPPTEST_ASSERT(data.size() == 1);
    CPPTEST_ASSERT(data.time[0] == 100.0);
    CPPTEST_ASSERT(data.open[0] == 10.0);
    CPPTEST_ASSERT(data.high[0] == 12.0);
    CPPTEST_ASSERT(data.low[0] == 9.0);
    CPPTEST_ASSERT(data.close[0] == 11.0);
    CPPTEST_ASSERT(data.volume[0] == 500.0);
    CPPTEST_ASSERT(data.bollinger_top.size() == 1);
    CPPTEST_ASSERT(data.bollinger_mid.size() == 1);
    CPPTEST_ASSERT(data.bollinger_bot.size() == 1);
    return 0;
}

int main() {
    CPPTEST_RUN(test_reserve);
    CPPTEST_RUN(test_append);
    return 0;
}
