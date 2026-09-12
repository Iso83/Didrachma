#include "common/market/FakeProvider.h"

#include "TestAssert.h"
#include "common/market/Series.h"

using namespace Didrachma::Testing;

int test_history_and_subscription() {
    FakeProvider provider({bar(0), bar(10), bar(20)});

    const auto history = provider.load_history({key(), {at(10), at(20)}});
    CPPTEST_ASSERT(std::get<std::vector<Bar>>(history).size() == 1);

    int calls = 0;
    auto subscription =
        provider.subscribe(key(), [&](const BarUpdate& update) { calls += static_cast<int>(update.bars.size()); });
    provider.emit({key(), BarUpdateKind::AppendClosed, {bar(30)}});
    CPPTEST_ASSERT(calls == 1);

    subscription.reset();
    provider.emit({key(), BarUpdateKind::AppendClosed, {bar(40)}});
    CPPTEST_ASSERT(calls == 1);
    return 0;
}

int main() {
    CPPTEST_RUN(test_history_and_subscription);
    return 0;
}
