#include "TestAssert.h"

#include <Didrachma/market/providers/yahoo/Provider.h>

using namespace Didrachma::Market::Providers::Yahoo;
using namespace Didrachma::Market::Core::Provider;
using namespace Didrachma::Market::Core::Time;

int test_capabilities_without_network() {
    Provider client;
    const auto capabilities = client.capabilities();
    CPPTEST_ASSERT(capabilities.supports(Capability::History));
    CPPTEST_ASSERT(!capabilities.supports(Capability::Streaming));

    const HistoryRequest unsupported{{"yahoo", "TEST", {10, Unit::Minute}},
                                     {UtcTimestamp{}, UtcTimestamp{std::chrono::hours{1}}}};
    CPPTEST_ASSERT(std::holds_alternative<Error>(client.load_history(unsupported)));
    return 0;
}

int main() {
    CPPTEST_RUN(test_capabilities_without_network);
    return 0;
}
