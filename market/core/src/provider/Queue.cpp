#include <Didrachma/market/core/provider/Queue.h>

using namespace Didrachma::Market::Core::Series;

namespace Didrachma::Market::Core::Provider {

void Queue::push(BarUpdate update) {
    std::scoped_lock lock(m_mutex);
    if (update.kind == BarUpdateKind::Reset)
        std::erase_if(m_updates, [&](const BarUpdate& queued) { return queued.key == update.key; });
    if (m_updates.size() == m_capacity)
        m_updates.pop_front();
    m_updates.push_back(std::move(update));
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
