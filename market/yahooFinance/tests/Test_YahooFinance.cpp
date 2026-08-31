#include "TestAssert.h"

#include <Didrachma/market/yahooFinance/YahooFinance.h>

using namespace Didrachma::Market::YahooFinance;

int test_default() {
    CPPTEST_ASSERT(true);
    return 0;
}

int main() {
    CPPTEST_RUN(test_default);
    return 0;
}
