#pragma once

#include <Didrachma/market/core/series/BarUpdate.h>
#include <cstdint>
#include <span>
#include <utility>

namespace Didrachma::Market::Core::Series {
struct RevisionAudit {
    Key key;
    std::uint64_t revision{};
    BarUpdateKind kind{BarUpdateKind::AppendClosed};
    Time::Range affected_range;
};

class Bars {
    Key m_key;
    std::vector<Bar> m_bars;
    std::uint64_t m_revision{};
    std::optional<Time::Range> m_dirty_range;
    std::optional<RevisionAudit> m_last_revision;

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
    [[nodiscard]] const std::optional<RevisionAudit>& last_revision() const {
        return m_last_revision;
    }

    bool apply(const BarUpdate& update);
    void clear_dirty_range() {
        m_dirty_range.reset();
    }
};
} // namespace Didrachma::Market::Core::Series
