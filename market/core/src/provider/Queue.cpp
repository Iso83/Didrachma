#include <Didrachma/market/core/provider/Queue.h>

using namespace Didrachma::Market::Core::Series;

namespace Didrachma::Market::Core::Provider {

void Queue::push(BarUpdate update) {
    std::scoped_lock lock(m_mutex);
    if (update.kind == BarUpdateKind::Reset) {
        const auto removed =
            std::erase_if(m_updates, [&](const BarUpdate& queued) { return queued.key == update.key; });
        m_coalesced += removed;
    } else if (update.kind == BarUpdateKind::ReplaceForming && update.bars.size() == 1) {
        const auto existing = std::ranges::find_if(m_updates, [&](const BarUpdate& queued) {
            return queued.kind == BarUpdateKind::ReplaceForming && queued.key == update.key &&
                   queued.bars.size() == 1 && queued.bars.front().open_time == update.bars.front().open_time;
        });
        if (existing != m_updates.end()) {
            *existing = std::move(update);
            ++m_coalesced;
            return;
        }
    }

    if (m_updates.size() == m_capacity) {
        const auto forming = std::ranges::find(m_updates, BarUpdateKind::ReplaceForming, &BarUpdate::kind);
        m_updates.erase(forming == m_updates.end() ? m_updates.begin() : forming);
        ++m_dropped;
    }
    m_updates.push_back(std::move(update));
    m_high_watermark = std::max(m_high_watermark, m_updates.size());
}

std::optional<BarUpdate> Queue::try_pop() {
    std::scoped_lock lock(m_mutex);
    if (m_updates.empty())
        return std::nullopt;

    auto result = std::move(m_updates.front());
    m_updates.pop_front();
    return result;
}

std::vector<BarUpdate> Queue::drain() {
    std::scoped_lock lock(m_mutex);
    std::vector<BarUpdate> result;
    result.reserve(m_updates.size());
    while (!m_updates.empty()) {
        result.push_back(std::move(m_updates.front()));
        m_updates.pop_front();
    }

    return result;
}
} // namespace Didrachma::Market::Core::Provider
