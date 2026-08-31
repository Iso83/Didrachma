#include "TestAssert.h"

#include <Didrachma/market/core/Core.h>

using namespace Didrachma::Market::Core;

int test_default() {
    CPPTEST_ASSERT(true);
    return 0;
}

int main() {
    CPPTEST_RUN(test_default);
    return 0;
}
