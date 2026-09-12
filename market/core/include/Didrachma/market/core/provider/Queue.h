#pragma once
#include <Didrachma/market/core/series/BarUpdate.h>
#include <deque>
#include <mutex>
#include <stdexcept>

namespace Didrachma::Market::Core::Provider {
class Queue {
    std::size_t m_capacity;
    std::deque<Series::BarUpdate> m_updates;
    mutable std::mutex m_mutex;

public:
    explicit Queue(std::size_t capacity) : m_capacity(capacity) {
        if (capacity == 0)
            throw std::invalid_argument("Queue capacity must be positive");
    }

    [[nodiscard]] std::size_t size() const {
        std::scoped_lock lock(m_mutex);
        return m_updates.size();
    }

    void push(Series::BarUpdate update);
    [[nodiscard]] std::optional<Series::BarUpdate> try_pop();
    [[nodiscard]] std::vector<Series::BarUpdate> drain();
};
} // namespace Didrachma::Market::Core::Provider
