#include "Polling.h"
#include "Request.h"
#include "TestAssert.h"

#include <Didrachma/market/core/series/Bars.h>
#include <Didrachma/market/providers/yahoo/Interval.h>
#include <Didrachma/market/providers/yahoo/Provider.h>
#include <array>

using namespace Didrachma::Market::Providers::Yahoo;
using namespace Didrachma::Market::Core::Provider;
using namespace Didrachma::Market::Core::Time;

int test_capabilities_without_network() {
    Provider client;
    const auto capabilities = client.capabilities();
    CPPTEST_ASSERT(capabilities.supports(Capability::History));
    CPPTEST_ASSERT(capabilities.supports(Capability::Polling));
    CPPTEST_ASSERT(!capabilities.supports(Capability::Streaming));

    const HistoryRequest unsupported{{"yahoo", "TEST", {5, Unit::Minute}},
                                     {UtcTimestamp{}, UtcTimestamp{std::chrono::hours{1}}}};
    CPPTEST_ASSERT(std::holds_alternative<Error>(client.load_history(unsupported)));
    return 0;
}

int test_polling_updates_forming_replacement_and_close() {
    using namespace Didrachma::Market::Providers::Yahoo::Intern;
    using namespace Didrachma::Market::Core::Series;
    const Key key{"yahoo", "TEST", {10, Unit::Minute}};
    const auto start = UtcTimestamp{std::chrono::hours{10}};
    const auto close = start + std::chrono::minutes{10};
    std::optional<Bar> previous;

    const Bar first{start, close, 1, 2, 0.5, 1.5, 100};
    auto updates = polling_updates(key, previous, std::array{first}, start + std::chrono::minutes{5});
    CPPTEST_ASSERT(updates.size() == 1 && updates[0].kind == BarUpdateKind::Backfill);
    CPPTEST_ASSERT(updates[0].bars[0].state == BarState::Forming);

    auto replacement = first;
    replacement.close = 1.75;
    replacement.volume = 150;
    updates = polling_updates(key, previous, std::array{replacement}, start + std::chrono::minutes{6});
    CPPTEST_ASSERT(updates.size() == 1 && updates[0].kind == BarUpdateKind::ReplaceForming);
    CPPTEST_ASSERT(updates[0].bars[0].volume == 150);

    updates = polling_updates(key, previous, std::array{replacement}, close);
    CPPTEST_ASSERT(updates.size() == 1 && updates[0].kind == BarUpdateKind::ReplaceForming);
    CPPTEST_ASSERT(updates[0].bars[0].state == BarState::Closed);
    CPPTEST_ASSERT(polling_updates(key, previous, std::array{replacement}, close).empty());

    Bars applied{key};
    previous.reset();
    auto forming = polling_updates(key, previous, std::array{first}, start + std::chrono::minutes{5});
    CPPTEST_ASSERT(applied.apply(forming[0]) && applied.bars()[0].state == BarState::Forming);
    auto closed = polling_updates(key, previous, std::array{replacement}, close);
    CPPTEST_ASSERT(applied.apply(closed[0]) && applied.bars()[0].state == BarState::Closed);
    CPPTEST_ASSERT(applied.bars()[0].close == 1.75 && applied.bars()[0].volume == 150);
    return 0;
}

int test_polling_folds_unaligned_quote_ticks_into_the_market_bar() {
    using namespace Didrachma::Market::Providers::Yahoo::Intern;
    using namespace Didrachma::Market::Core::Series;
    const Key key{"yahoo", "TEST", {1, Unit::Minute}};
    const auto minute = UtcTimestamp{std::chrono::seconds{60}};
    const std::array response{
        Bar{minute, minute + std::chrono::minutes{1}, 10, 12, 9, 11, 100},
        Bar{minute + std::chrono::seconds{44}, minute + std::chrono::seconds{45}, 11, 13, 10, 12.5, 0}};
    std::optional<Bar> previous;
    const auto updates = polling_updates(key, previous, response, minute + std::chrono::seconds{45});
    CPPTEST_ASSERT(updates.size() == 1 && updates[0].bars.size() == 1);
    const auto& bar = updates[0].bars[0];
    CPPTEST_ASSERT(bar.open_time == minute && bar.close_time == minute + std::chrono::minutes{1});
    CPPTEST_ASSERT(bar.open == 10 && bar.high == 13 && bar.low == 9 && bar.close == 12.5 && bar.volume == 100);
    CPPTEST_ASSERT(bar.state == BarState::Forming);
    return 0;
}

int test_polling_range_requests_a_small_recent_window() {
    using Didrachma::Market::Providers::Yahoo::Intern::polling_range;
    const auto now = UtcTimestamp{std::chrono::days{100}};
    const auto intraday = polling_range({10, Unit::Minute}, now);
    CPPTEST_ASSERT(intraday.begin == now - std::chrono::hours{24});
    CPPTEST_ASSERT(intraday.end == now + std::chrono::minutes{10});
    const auto daily = polling_range({1, Unit::Day}, now);
    CPPTEST_ASSERT(daily.begin == now - std::chrono::days{3});
    CPPTEST_ASSERT(daily.end == now + std::chrono::days{1});
    return 0;
}

int test_supported_intervals_and_cache_identity() {
    using Didrachma::Market::Providers::Yahoo::Intern::cache_filename;
    const std::array expected{std::pair{Frame{1, Unit::Minute}, "1m"},   std::pair{Frame{2, Unit::Minute}, "2m"},
                              std::pair{Frame{5, Unit::Minute}, "5m"},   std::pair{Frame{10, Unit::Minute}, "5m"},
                              std::pair{Frame{15, Unit::Minute}, "15m"}, std::pair{Frame{30, Unit::Minute}, "30m"},
                              std::pair{Frame{1, Unit::Hour}, "1h"},     std::pair{Frame{90, Unit::Minute}, "90m"},
                              std::pair{Frame{1, Unit::Day}, "1d"},      std::pair{Frame{5, Unit::Day}, "5d"},
                              std::pair{Frame{7, Unit::Day}, "1wk"},     std::pair{Frame{30, Unit::Day}, "1mo"},
                              std::pair{Frame{90, Unit::Day}, "3mo"}};
    CPPTEST_ASSERT(intervals().size() == expected.size());
    for (const auto& [frame, value] : expected) {
        const auto* interval = find_interval(frame);
        CPPTEST_ASSERT(interval && interval->value == value);
    }
    CPPTEST_ASSERT(find_interval({10, Unit::Minute})->derived());
    CPPTEST_ASSERT((find_interval({10, Unit::Minute})->source_frame == Frame{5, Unit::Minute}));
    CPPTEST_ASSERT(find_interval({10, Unit::Minute})->value != "10m");
    CPPTEST_ASSERT(!find_interval({4, Unit::Minute}));
    CPPTEST_ASSERT(find_interval({1, Unit::Minute})->history_reach == std::chrono::days{8});
    CPPTEST_ASSERT(find_interval({10, Unit::Minute})->history_reach == std::chrono::days{60});
    CPPTEST_ASSERT(find_interval({1, Unit::Hour})->history_reach == std::chrono::days{730});
    CPPTEST_ASSERT(!find_interval({1, Unit::Day})->history_reach);

    const Range range{UtcTimestamp{}, UtcTimestamp{std::chrono::days{1}}};
    const HistoryRequest daily{{"yahoo", "TEST", {1, Unit::Day}}, range};
    const HistoryRequest weekly{{"yahoo", "TEST", {7, Unit::Day}}, range};
    const HistoryRequest five{{"yahoo", "TEST", {5, Unit::Minute}}, range};
    const HistoryRequest ten{{"yahoo", "TEST", {10, Unit::Minute}}, range};
    CPPTEST_ASSERT(cache_filename(daily) != cache_filename(weekly));
    CPPTEST_ASSERT(cache_filename(five) != cache_filename(ten));
    return 0;
}

int test_history_range_validation() {
    const auto now = UtcTimestamp{std::chrono::days{1000}};
    CPPTEST_ASSERT(validate_history({1, Unit::Minute}, {now - std::chrono::days{8}, now}, now).valid);
    const auto old =
        validate_history({1, Unit::Minute}, {now - std::chrono::days{8} - std::chrono::seconds{1}, now}, now);
    CPPTEST_ASSERT(!old.valid && old.earliest_allowed == now - std::chrono::days{8});
    CPPTEST_ASSERT(validate_history({1, Unit::Day}, {now - std::chrono::days{5000}, now}, now).valid);
    CPPTEST_ASSERT(!validate_history({1, Unit::Day}, {now, now + std::chrono::days{1}}, now).valid);
    CPPTEST_ASSERT(!validate_history({4, Unit::Minute}, {now - std::chrono::days{1}, now}, now).valid);

    const auto midday = now + std::chrono::hours{12};
    const auto minute = suggested_history_range({1, Unit::Minute}, midday);
    CPPTEST_ASSERT(minute.begin == now - std::chrono::days{7} && minute.end == now + std::chrono::days{1});
    const auto hourly = suggested_history_range({1, Unit::Hour}, midday);
    CPPTEST_ASSERT(hourly.begin == now - std::chrono::days{729} && hourly.end == now + std::chrono::days{1});
    const auto daily = suggested_history_range({1, Unit::Day}, midday);
    CPPTEST_ASSERT(daily.begin == daily.end - std::chrono::days{365 * 5} && daily.end == now + std::chrono::days{1});
    return 0;
}

int main() {
    CPPTEST_RUN(test_capabilities_without_network);
    CPPTEST_RUN(test_supported_intervals_and_cache_identity);
    CPPTEST_RUN(test_history_range_validation);
    CPPTEST_RUN(test_polling_updates_forming_replacement_and_close);
    CPPTEST_RUN(test_polling_folds_unaligned_quote_ticks_into_the_market_bar);
    CPPTEST_RUN(test_polling_range_requests_a_small_recent_window);
    return 0;
}
