#include "TestAssert.h"
#include "common\market\Series.h"

#include <Didrachma/market/core/provider/Queue.h>
#include <thread>

using namespace Didrachma::Testing;
using namespace Didrachma::Market::Core::Provider;

int test_bounded_thread_safe_delivery() {
    Queue queue(2);
    std::thread producer([&] {
        queue.push({key(), BarUpdateKind::AppendClosed, {bar(0)}});
        queue.push({key(), BarUpdateKind::AppendClosed, {bar(10)}});
        queue.push({key(), BarUpdateKind::AppendClosed, {bar(20)}});
    });
    producer.join();

    const auto updates = queue.drain();
    CPPTEST_ASSERT(updates.size() == 2);
    CPPTEST_ASSERT(updates[0].bars[0].open_time == at(10));
    return 0;
}

int main() {
    CPPTEST_RUN(test_bounded_thread_safe_delivery);
    return 0;
}
