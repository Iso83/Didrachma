#include "TestAssert.h"
#include "common/market/Series.h"

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

int test_reset_coalesces_only_matching_series() {
    Queue queue(4);
    const Key other{"fake", "OTHER", {10, Unit::Minute}};
    queue.push({key(), BarUpdateKind::AppendClosed, {bar(0)}});
    queue.push({other, BarUpdateKind::AppendClosed, {bar(0)}});
    queue.push({key(), BarUpdateKind::Reset, {bar(20)}});

    const auto updates = queue.drain();
    CPPTEST_ASSERT(updates.size() == 2);
    CPPTEST_ASSERT(updates[0].key == other);
    CPPTEST_ASSERT(updates[1].kind == BarUpdateKind::Reset);
    return 0;
}

int test_forming_updates_coalesce_without_losing_close() {
    Queue queue(2);
    queue.push({key(), BarUpdateKind::ReplaceForming, {bar(0, BarState::Forming, 1)}});
    queue.push({key(), BarUpdateKind::ReplaceForming, {bar(0, BarState::Forming, 2)}});
    queue.push({key(), BarUpdateKind::AppendClosed, {bar(10, BarState::Closed, 3)}});

    const auto statistics = queue.statistics();
    const auto updates = queue.drain();
    CPPTEST_ASSERT(updates.size() == 2 && updates[0].bars[0].close == 2);
    CPPTEST_ASSERT(updates[1].kind == BarUpdateKind::AppendClosed);
    CPPTEST_ASSERT(statistics.coalesced == 1 && statistics.dropped == 0 && statistics.high_watermark == 2);
    return 0;
}

int test_overflow_discards_provisional_update_before_closed_data() {
    Queue queue(2);
    queue.push({key(), BarUpdateKind::ReplaceForming, {bar(0, BarState::Forming)}});
    queue.push({key(), BarUpdateKind::AppendClosed, {bar(10)}});
    queue.push({key(), BarUpdateKind::AppendClosed, {bar(20)}});

    const auto updates = queue.drain();
    CPPTEST_ASSERT(updates.size() == 2 && updates[0].bars[0].open_time == at(10));
    CPPTEST_ASSERT(queue.statistics().dropped == 1);
    return 0;
}

int main() {
    CPPTEST_RUN(test_bounded_thread_safe_delivery);
    CPPTEST_RUN(test_reset_coalesces_only_matching_series);
    CPPTEST_RUN(test_forming_updates_coalesce_without_losing_close);
    CPPTEST_RUN(test_overflow_discards_provisional_update_before_closed_data);
    return 0;
}
