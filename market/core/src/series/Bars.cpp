#include <Didrachma/market/core/series/Bars.h>
#include <algorithm>

using namespace Didrachma::Market::Core::Time;

namespace Didrachma::Market::Core::Series {
Range affected_range(const std::vector<Bar>& bars) {
    const auto [first, last] = std::ranges::minmax_element(bars, {}, &Bar::open_time);
    return {first->open_time, last->close_time.value_or(last->open_time + std::chrono::seconds{1})};
}

bool Bars::apply(const BarUpdate& update) {
    if (update.key != m_key || update.bars.empty())
        return false;

    auto incoming = update.bars;
    std::ranges::sort(incoming, {}, &Bar::open_time);
    incoming.erase(std::unique(incoming.begin(), incoming.end(),
                               [](const Bar& a, const Bar& b) { return a.open_time == b.open_time; }),
                   incoming.end());

    std::optional<Range> replaced_range;
    bool changed = false;
    if (update.kind == BarUpdateKind::Reset) {
        if (!m_bars.empty())
            replaced_range = affected_range(m_bars);
        m_bars = std::move(incoming);
        changed = true;
    } else {
        for (const auto& bar : incoming) {
            if (update.kind == BarUpdateKind::AppendClosed &&
                (bar.state != BarState::Closed || (!m_bars.empty() && bar.open_time <= m_bars.back().open_time)))
                continue;

            auto at = std::ranges::lower_bound(m_bars, bar.open_time, {}, &Bar::open_time);
            if (at == m_bars.end() || at->open_time != bar.open_time) {
                if (update.kind == BarUpdateKind::ReplaceForming)
                    continue;

                m_bars.insert(at, bar);
                changed = true;
            } else if (update.kind == BarUpdateKind::Backfill ||
                       (update.kind == BarUpdateKind::ReplaceForming && at->state == BarState::Forming)) {
                *at = bar;
                changed = true;
            }
        }
    }
    if (!changed)
        return false;

    auto dirty = affected_range(update.bars);
    if (replaced_range) {
        dirty.begin = std::min(dirty.begin, replaced_range->begin);
        dirty.end = std::max(dirty.end, replaced_range->end);
    }

    if (m_dirty_range) {
        m_dirty_range->begin = std::min(m_dirty_range->begin, dirty.begin);
        m_dirty_range->end = std::max(m_dirty_range->end, dirty.end);
    } else
        m_dirty_range = dirty;
    ++m_revision;
    return true;
}
} // namespace Didrachma::Market::Core::Series
