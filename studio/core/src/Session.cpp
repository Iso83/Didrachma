#include <Didrachma/studio/core/Session.h>
#include <algorithm>

namespace Didrachma::Studio::Core {
Session::Session(Market::Core::Provider::Data& provider, Analysis::Core::Indicator::Analyzer& analyzer,
                 StockChart::Core::Document document)
    : m_provider(&provider), m_analyzer(&analyzer), m_document(std::move(document)), m_bars(m_document.series()) {}

Session::~Session() {
    stop();
}

std::optional<Market::Core::Provider::Error> Session::load_history() {
    auto result = m_provider->load_history({m_document.series(), m_document.visible_range()});
    if (auto* error = std::get_if<Market::Core::Provider::Error>(&result))
        return *error;

    m_queue.push({m_document.series(), Market::Core::Series::BarUpdateKind::Reset,
                  std::move(std::get<std::vector<Market::Core::Series::Bar>>(result))});
    return std::nullopt;
}

bool Session::load_history_async(std::size_t maximum_attempts, std::chrono::milliseconds initial_backoff) {
    if (!m_provider->capabilities().supports(Market::Core::Provider::Capability::History) || maximum_attempts == 0 ||
        m_history_worker.joinable())
        return false;

    {
        std::scoped_lock lock(m_status_mutex);
        m_connection = ConnectionState::LoadingHistory;
        m_message.clear();
    }
    m_history_worker = std::jthread([this, maximum_attempts, initial_backoff](std::stop_token stop) {
        for (std::size_t attempt = 0; attempt < maximum_attempts && !stop.stop_requested(); ++attempt) {
            {
                std::scoped_lock lock(m_status_mutex);
                ++m_history_attempts;
            }
            auto result = m_provider->load_history({m_document.series(), m_document.visible_range()});
            if (auto* bars = std::get_if<std::vector<Market::Core::Series::Bar>>(&result)) {
                m_queue.push({m_document.series(), Market::Core::Series::BarUpdateKind::Reset, std::move(*bars)});
                std::scoped_lock lock(m_status_mutex);
                m_connection = ConnectionState::Idle;
                m_message.clear();
                return;
            }

            {
                std::scoped_lock lock(m_status_mutex);
                m_connection = ConnectionState::Error;
                m_message = std::get<Market::Core::Provider::Error>(result).message;
            }
            if (attempt + 1 < maximum_attempts) {
                std::unique_lock lock(m_status_mutex);
                const auto delay = initial_backoff * (std::size_t{1} << std::min<std::size_t>(attempt, 10));
                m_retry_wait.wait_for(lock, stop, delay, [] { return false; });
                if (!stop.stop_requested())
                    m_connection = ConnectionState::LoadingHistory;
            }
        }
    });
    return true;
}

bool Session::start_live() {
    const auto capabilities = m_provider->capabilities();
    if (!capabilities.supports(Market::Core::Provider::Capability::Streaming) &&
        !capabilities.supports(Market::Core::Provider::Capability::Polling))
        return false;

    m_subscription =
        m_provider->subscribe(m_document.series(), [this](auto update) { m_queue.push(std::move(update)); });
    std::scoped_lock lock(m_status_mutex);
    m_connection = m_subscription ? (capabilities.supports(Market::Core::Provider::Capability::Streaming)
                                         ? ConnectionState::Live
                                         : ConnectionState::Polling)
                                  : ConnectionState::Error;
    m_message = m_subscription ? std::string{} : "Provider could not start live updates";
    return m_subscription != nullptr;
}

void Session::stop() {
    m_subscription.reset();
    if (m_history_worker.joinable()) {
        m_history_worker.request_stop();
        m_retry_wait.notify_all();
        m_history_worker.join();
    }
    std::scoped_lock lock(m_status_mutex);
    m_connection = ConnectionState::Stopped;
}

SessionStatus Session::status() const {
    std::scoped_lock lock(m_status_mutex);
    return {m_connection, m_message, m_history_attempts, m_calculation_time, m_queue.statistics()};
}

FrameUpdate Session::apply_frame_updates() {
    FrameUpdate frame;
    for (auto& update : m_queue.drain()) {
        if (m_bars.apply(update))
            ++frame.applied_updates;
    }
    frame.dirty_range = m_bars.dirty_range();
    frame.series_revision = m_bars.revision();
    if (frame.dirty_range) {
        const auto visible = m_document.visible_range();
        frame.render_data_invalidated =
            frame.dirty_range->begin < visible.end && visible.begin < frame.dirty_range->end;
        if (!frame.render_data_invalidated && frame.dirty_range->begin >= visible.end) {
            if (m_document.auto_follow()) {
                const auto duration = visible.end - visible.begin;
                m_document.dispatch(
                    StockChart::Core::NavigateViewport{{frame.dirty_range->end - duration, frame.dirty_range->end}});
                frame.render_data_invalidated = true;
                m_newer_data_available = false;
            } else
                m_newer_data_available = true;
        }
    }
    const auto calculation_begin = std::chrono::steady_clock::now();
    for (const auto& item : m_document.indicators()) {
        auto& entry = *m_document.find_indicator(item.instance.id);
        if (!entry.instance.enabled)
            continue;

        auto outcome = m_analyzer->calculate({entry.instance, m_bars.bars(), m_bars.revision(), m_bars.dirty_range()});
        entry.cached_result = std::move(outcome.result);
        ++frame.calculated_indicators;
    }
    frame.calculation_time = std::chrono::steady_clock::now() - calculation_begin;
    frame.newer_data_available = m_newer_data_available;
    {
        std::scoped_lock lock(m_status_mutex);
        m_calculation_time += frame.calculation_time;
    }
    m_bars.clear_dirty_range();
    return frame;
}
} // namespace Didrachma::Studio::Core
