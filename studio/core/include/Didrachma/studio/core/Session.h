#pragma once

#include <Didrachma/analysis/core/indicator/Analyzer.h>
#include <Didrachma/market/core/provider/Data.h>
#include <Didrachma/market/core/provider/Queue.h>
#include <Didrachma/market/core/series/Bars.h>
#include <Didrachma/stockChart/core/Document.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace Didrachma::Studio::Core {
struct FrameUpdate {
    std::size_t applied_updates{};
    std::size_t calculated_indicators{};
    std::optional<Market::Core::Time::Range> dirty_range;
    std::uint64_t series_revision{};
    bool render_data_invalidated{};
    bool newer_data_available{};
    std::chrono::nanoseconds calculation_time{};
};

enum class ConnectionState { Idle, LoadingHistory, Polling, Live, Error, Stopped };

struct SessionStatus {
    ConnectionState connection{ConnectionState::Idle};
    std::string message;
    std::uint64_t history_attempts{};
    std::chrono::nanoseconds calculation_time{};
    Market::Core::Provider::Queue::Statistics queue;
};

class Session {
    Market::Core::Provider::Data* m_provider;
    Analysis::Core::Indicator::Analyzer* m_analyzer;
    StockChart::Core::Document m_document;
    Market::Core::Series::Bars m_bars;
    Market::Core::Provider::Queue m_queue{256};
    std::unique_ptr<Market::Core::Provider::Subscription> m_subscription;
    std::jthread m_history_worker;
    mutable std::mutex m_status_mutex;
    std::condition_variable_any m_retry_wait;
    ConnectionState m_connection{ConnectionState::Idle};
    std::string m_message;
    std::uint64_t m_history_attempts{};
    std::chrono::nanoseconds m_calculation_time{};
    bool m_newer_data_available{};

public:
    Session(Market::Core::Provider::Data& provider, Analysis::Core::Indicator::Analyzer& analyzer,
            StockChart::Core::Document document);
    ~Session();
    Session(const Session&) = delete;

    Session& operator=(const Session&) = delete;

    [[nodiscard]] StockChart::Core::Document& document() {
        return m_document;
    }
    [[nodiscard]] const Market::Core::Series::Bars& bars() const {
        return m_bars;
    }
    [[nodiscard]] Market::Core::Provider::Queue& updates() {
        return m_queue;
    }

    std::optional<Market::Core::Provider::Error> load_history();
    bool load_history_async(std::size_t maximum_attempts = 3,
                            std::chrono::milliseconds initial_backoff = std::chrono::milliseconds{100});
    bool start_live();
    void stop();
    [[nodiscard]] SessionStatus status() const;
    FrameUpdate apply_frame_updates();
};
} // namespace Didrachma::Studio::Core
