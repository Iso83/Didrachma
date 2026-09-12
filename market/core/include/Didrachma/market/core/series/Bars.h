#pragma once

#include <Didrachma/market/core/series/BarUpdate.h>
#include <cstdint>
#include <span>

namespace Didrachma::Market::Core::Series {
class Bars {
    Key m_key;
    std::vector<Bar> m_bars;
    std::uint64_t m_revision{};
    std::optional<Time::Range> m_dirty_range;

public:
    explicit Bars(Key key) : m_key(std::move(key)) {}

    [[nodiscard]] const Key& key() const {
        return m_key;
    }
    [[nodiscard]] std::span<const Bar> bars() const {
        return m_bars;
    }
    [[nodiscard]] std::uint64_t revision() const {
        return m_revision;
    }
    [[nodiscard]] const std::optional<Time::Range>& dirty_range() const {
        return m_dirty_range;
    }

    bool apply(const BarUpdate& update);
    void clear_dirty_range() {
        m_dirty_range.reset();
    }
};
} // namespace Didrachma::Market::Core::Series
