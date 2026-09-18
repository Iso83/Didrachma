#include "TestAssert.h"
#include "common/market/FakeProvider.h"

#include <Didrachma/studio/core/Session.h>
#include <atomic>
#include <thread>

using namespace Didrachma;
using namespace Didrachma::Market::Core;
Time::UtcTimestamp at(int seconds) {
    return Time::UtcTimestamp{std::chrono::seconds{seconds}};
}

class Analyzer final : public Analysis::Core::Indicator::Analyzer {
public:
    std::vector<Analysis::Core::Indicator::Definition> catalog() const override {
        return {};
    }

    Analysis::Core::Indicator::CalculationOutcome
    calculate(const Analysis::Core::Indicator::CalculationRequest& request) override {
        return {
            {{}, request.input_revision, request.input_revision, Analysis::Core::Indicator::CalculationState::Ready},
            Analysis::Core::Indicator::RecalculationKind::Full};
    }
};

int test_frame_boundary_applies_values_and_analysis() {
    const Series::Key key{"fake", "TEST", {1, Time::Unit::Day}};
    Testing::FakeProvider provider({{at(0), at(1), 1, 2, 0, 1, 10}});
    Analyzer analyzer;
    StockChart::Core::Document document{"chart", key, {at(0), at(100)}};
    document.add_indicator("sma", {});

    Studio::Core::Session session{provider, analyzer, std::move(document)};
    CPPTEST_ASSERT(!session.load_history() && session.bars().bars().empty());

    const auto frame = session.apply_frame_updates();
    CPPTEST_ASSERT(frame.applied_updates == 1 && frame.calculated_indicators == 1 && frame.series_revision == 1);
    CPPTEST_ASSERT(session.bars().bars().size() == 1 && session.document().indicators()[0].cached_result.has_value());
    return 0;
}

int test_streaming_capability_queue_and_clean_shutdown() {
    const Series::Key key{"fake", "TEST", {1, Time::Unit::Day}};
    Testing::FakeProvider provider;
    Analyzer analyzer;
    {
        StockChart::Core::Document document{"chart", key, {at(0), at(100)}};
        Studio::Core::Session session{provider, analyzer, std::move(document)};
        CPPTEST_ASSERT(session.start_live() && provider.subscriber_count() == 1);
        provider.emit({key, Series::BarUpdateKind::Backfill, {{at(0), at(1), 1, 2, 0, 1, 10}}});
        CPPTEST_ASSERT(session.status().connection == Studio::Core::ConnectionState::Live);
        CPPTEST_ASSERT(session.apply_frame_updates().applied_updates == 1);
    }
    CPPTEST_ASSERT(provider.subscriber_count() == 0);
    return 0;
}

int test_offscreen_updates_continue_without_render_invalidation() {
    const Series::Key key{"fake", "TEST", {1, Time::Unit::Day}};
    Testing::FakeProvider provider;
    Analyzer analyzer;
    StockChart::Core::Document document{"chart", key, {at(0), at(100)}};
    document.set_auto_follow(false);
    document.add_indicator("sma", {});
    Studio::Core::Session session{provider, analyzer, std::move(document)};
    session.updates().push({key, Series::BarUpdateKind::Backfill, {{at(200), at(201), 1, 2, 0, 1, 10}}});

    const auto frame = session.apply_frame_updates();
    CPPTEST_ASSERT(frame.applied_updates == 1 && frame.calculated_indicators == 1);
    CPPTEST_ASSERT(!frame.render_data_invalidated && frame.newer_data_available);
    return 0;
}

int test_auto_follow_advances_viewport_for_new_data() {
    const Series::Key key{"fake", "TEST", {1, Time::Unit::Day}};
    Testing::FakeProvider provider;
    Analyzer analyzer;
    StockChart::Core::Document document{"chart", key, {at(0), at(100)}};
    Studio::Core::Session session{provider, analyzer, std::move(document)};
    session.updates().push({key, Series::BarUpdateKind::Backfill, {{at(200), at(201), 1, 2, 0, 1, 10}}});

    const auto frame = session.apply_frame_updates();
    CPPTEST_ASSERT(frame.render_data_invalidated && !frame.newer_data_available);
    CPPTEST_ASSERT(session.document().visible_range().begin == at(101));
    CPPTEST_ASSERT(session.document().visible_range().end == at(201));
    return 0;
}

class RetryingProvider final : public Market::Core::Provider::Data {
    std::atomic<int> m_attempts{};

public:
    Market::Core::Provider::CapabilitySet capabilities() const override {
        return Market::Core::Provider::CapabilitySet::from(Market::Core::Provider::Capability::History);
    }
    Market::Core::Provider::HistoryResult load_history(const Market::Core::Provider::HistoryRequest&) override {
        if (++m_attempts < 3)
            return Market::Core::Provider::Error{"temporary"};
        return std::vector<Series::Bar>{{at(0), at(1), 1, 2, 0, 1, 10}};
    }
    std::unique_ptr<Market::Core::Provider::Subscription> subscribe(const Series::Key&,
                                                                    Market::Core::Provider::UpdateHandler) override {
        return {};
    }
};

class PollingProvider final : public Market::Core::Provider::Data {
    Testing::FakeProvider m_provider;

public:
    Market::Core::Provider::CapabilitySet capabilities() const override {
        return Market::Core::Provider::Capability::History | Market::Core::Provider::Capability::Polling;
    }
    Market::Core::Provider::HistoryResult load_history(const Market::Core::Provider::HistoryRequest& request) override {
        return m_provider.load_history(request);
    }
    std::unique_ptr<Market::Core::Provider::Subscription>
    subscribe(const Series::Key& key, Market::Core::Provider::UpdateHandler handler) override {
        return m_provider.subscribe(key, std::move(handler));
    }
};

int test_polling_is_reported_truthfully_instead_of_live() {
    const Series::Key key{"polling", "TEST", {1, Time::Unit::Day}};
    PollingProvider provider;
    Analyzer analyzer;
    StockChart::Core::Document document{"chart", key, {at(0), at(100)}};
    Studio::Core::Session session{provider, analyzer, std::move(document)};
    CPPTEST_ASSERT(session.start_live());
    CPPTEST_ASSERT(session.status().connection == Studio::Core::ConnectionState::Polling);
    return 0;
}

int test_async_history_retries_and_publishes_at_frame_boundary() {
    const Series::Key key{"retry", "TEST", {1, Time::Unit::Day}};
    RetryingProvider provider;
    Analyzer analyzer;
    StockChart::Core::Document document{"chart", key, {at(0), at(100)}};
    Studio::Core::Session session{provider, analyzer, std::move(document)};
    CPPTEST_ASSERT(session.load_history_async(3, std::chrono::milliseconds{1}));
    for (int wait = 0; wait < 100 && session.status().history_attempts < 3; ++wait)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});

    CPPTEST_ASSERT(session.status().history_attempts == 3);
    CPPTEST_ASSERT(session.apply_frame_updates().applied_updates == 1);
    CPPTEST_ASSERT(session.bars().bars().size() == 1);
    return 0;
}

int test_load_many_charts_and_forming_updates_stays_bounded() {
    Analyzer analyzer;
    Testing::FakeProvider provider;
    std::vector<std::unique_ptr<Studio::Core::Session>> sessions;
    for (int chart = 0; chart < 24; ++chart) {
        Series::Key key{"fake", "TEST-" + std::to_string(chart), {1, Time::Unit::Day}};
        StockChart::Core::Document document{"chart-" + std::to_string(chart), key, {at(0), at(20000)}};
        for (int indicator = 0; indicator < 20; ++indicator)
            document.add_indicator("sma", {});
        sessions.push_back(std::make_unique<Studio::Core::Session>(provider, analyzer, std::move(document)));
        for (int update = 0; update < 1000; ++update)
            sessions.back()->updates().push(
                {key,
                 Series::BarUpdateKind::ReplaceForming,
                 {{at(100), at(101), 1, 2, 0, static_cast<double>(update), 10, Series::BarState::Forming}}});
        sessions.back()->updates().push(
            {key, Series::BarUpdateKind::Backfill, {{at(100), at(101), 1, 2, 0, 1000, 10}}});
    }
    for (auto& session : sessions) {
        CPPTEST_ASSERT(session->status().queue.depth == 2);
        CPPTEST_ASSERT(session->status().queue.coalesced == 999);
        CPPTEST_ASSERT(session->apply_frame_updates().calculated_indicators == 20);
    }
    return 0;
}

int test_long_history_is_applied_as_one_bounded_update() {
    const Series::Key key{"fake", "LONG", {1, Time::Unit::Day}};
    Testing::FakeProvider provider;
    Analyzer analyzer;
    StockChart::Core::Document document{"long-chart", key, {at(0), at(200000)}};
    document.add_indicator("sma", {});
    Studio::Core::Session session{provider, analyzer, std::move(document)};
    std::vector<Series::Bar> history;
    history.reserve(100000);
    for (int index = 0; index < 100000; ++index)
        history.push_back({at(index), at(index + 1), 1, 2, 0, 1, 10});
    session.updates().push({key, Series::BarUpdateKind::Reset, std::move(history)});

    const auto frame = session.apply_frame_updates();
    CPPTEST_ASSERT(frame.applied_updates == 1 && session.bars().bars().size() == 100000);
    CPPTEST_ASSERT(frame.calculated_indicators == 1 && session.status().queue.high_watermark == 1);
    return 0;
}

int main() {
    CPPTEST_RUN(test_frame_boundary_applies_values_and_analysis);
    CPPTEST_RUN(test_streaming_capability_queue_and_clean_shutdown);
    CPPTEST_RUN(test_offscreen_updates_continue_without_render_invalidation);
    CPPTEST_RUN(test_auto_follow_advances_viewport_for_new_data);
    CPPTEST_RUN(test_async_history_retries_and_publishes_at_frame_boundary);
    CPPTEST_RUN(test_polling_is_reported_truthfully_instead_of_live);
    CPPTEST_RUN(test_load_many_charts_and_forming_updates_stays_bounded);
    CPPTEST_RUN(test_long_history_is_applied_as_one_bounded_update);
    return 0;
}
