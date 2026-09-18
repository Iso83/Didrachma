#include <Didrachma/stockChart/render/State.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Didrachma::StockChart::Render {
int pane_content_top(int pane_top) {
    return pane_top + pane_header_height;
}

int pane_content_height(int pane_height) {
    return std::max(1, pane_height - pane_header_height);
}

PaneLayout linked_pane_layout(Size size, float price_ratio, int splitter_height, int minimum_pane_height) {
    const auto usable = std::max(0, size.height - splitter_height);
    const auto minimum = std::min(minimum_pane_height, usable / 2);
    const auto price =
        std::clamp(static_cast<int>(usable * std::clamp(price_ratio, 0.0F, 1.0F)), minimum, usable - minimum);
    return {price, price, price + splitter_height, usable - price, 0, price, price, price, 0, size.width};
}

float price_pane_ratio_from_splitter(float pointer_y, Size size, int splitter_height, int minimum_pane_height) {
    const auto usable = std::max(0, size.height - splitter_height);
    if (usable == 0)
        return 0.5F;

    const auto minimum = std::min(minimum_pane_height, usable / 2);
    const auto price = std::clamp(pointer_y, static_cast<float>(minimum), static_cast<float>(usable - minimum));
    return price / static_cast<float>(usable);
}

PaneLayout with_separate_pane(PaneLayout panes, float price_ratio, int splitter_height, int minimum_pane_height) {
    const auto usable = std::max(0, panes.price_height - splitter_height);
    const auto minimum = std::min(minimum_pane_height, usable / 2);
    panes.price_content_height =
        std::clamp(static_cast<int>(usable * std::clamp(price_ratio, 0.0F, 1.0F)), minimum, usable - minimum);
    panes.separate_top = 0;
    panes.separate_height = usable - panes.price_content_height;
    panes.separate_splitter_y = panes.separate_height;
    panes.price_top = panes.separate_splitter_y + splitter_height;
    return panes;
}

float separate_pane_ratio_from_splitter(float pointer_y, PaneLayout panes, int splitter_height,
                                        int minimum_pane_height) {
    const auto usable = std::max(0, panes.price_height - splitter_height);
    if (usable == 0)
        return 0.5F;

    const auto minimum = std::min(minimum_pane_height, usable / 2);
    const auto separate = std::clamp(pointer_y, static_cast<float>(minimum), static_cast<float>(usable - minimum));
    const auto price = static_cast<float>(usable) - separate;
    return price / static_cast<float>(usable);
}

float axis_label_left(float minimum_x, float maximum_x, float center_x, float label_width) {
    const auto bounded_maximum = std::max(minimum_x, maximum_x);
    const auto available_width = bounded_maximum - minimum_x;
    const auto fitted_width = std::clamp(label_width, 0.0F, available_width);
    return std::clamp(center_x - fitted_width * 0.5F, minimum_x, bounded_maximum - fitted_width);
}

PixelColumnRange covering_pixel_columns(float first, float second) {
    return {static_cast<int>(std::floor(std::min(first, second))),
            static_cast<int>(std::ceil(std::max(first, second)))};
}

bool mapping_is_valid(Market::Core::Time::Range time, PriceRange price, Size size) {
    return !time.empty() && price.minimum < price.maximum && size.width > 0 && size.height > 0;
}

CoordinateMapper::CoordinateMapper(Market::Core::Time::Range time, PriceRange price, Size size, float y_offset)
    : m_time(time), m_price(price), m_size(size), m_y_offset(y_offset) {
    if (!mapping_is_valid(time, price, size))
        throw std::invalid_argument("Coordinate mapper ranges and size must be non-empty");
}

Point CoordinateMapper::map(Market::Core::Time::UtcTimestamp time, double price) const {
    const auto offset = std::chrono::duration<double>(time - m_time.begin).count();
    const auto duration = std::chrono::duration<double>(m_time.end - m_time.begin).count();
    return {static_cast<float>(offset / duration * m_size.width),
            m_y_offset +
                static_cast<float>((m_price.maximum - price) / (m_price.maximum - m_price.minimum) * m_size.height)};
}

std::pair<Market::Core::Time::UtcTimestamp, double> CoordinateMapper::unmap(Point point) const {
    const auto duration = m_time.end - m_time.begin;
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration * (point.x / m_size.width));
    return {m_time.begin + seconds,
            m_price.maximum - ((point.y - m_y_offset) / m_size.height) * (m_price.maximum - m_price.minimum)};
}

bool InvalidationState::invalidate_data(const Market::Core::Time::Range& dirty,
                                        const Market::Core::Time::Range& visible, bool auto_follow) {
    const bool intersects = dirty.begin < visible.end && visible.begin < dirty.end;
    if (intersects || auto_follow)
        invalidate(DirtyReason::Data);
    return intersects || auto_follow;
}

std::span<const Market::Core::Series::Bar> visible_bars(std::span<const Market::Core::Series::Bar> bars,
                                                        Market::Core::Time::Range range) {
    const auto begin = std::lower_bound(bars.begin(), bars.end(), range.begin,
                                        [](const auto& bar, auto time) { return bar.open_time < time; });
    const auto end =
        std::lower_bound(begin, bars.end(), range.end, [](const auto& bar, auto time) { return bar.open_time < time; });
    return {begin, end};
}

PriceRange fit_price_range(std::span<const Market::Core::Series::Bar> bars, double padding_fraction) {
    if (bars.empty())
        return {0.0, 1.0};

    auto minimum = bars.front().low;
    auto maximum = bars.front().high;
    for (const auto& bar : bars) {
        minimum = std::min(minimum, bar.low);
        maximum = std::max(maximum, bar.high);
    }
    const auto span = std::max(maximum - minimum, std::max(std::abs(maximum), 1.0) * 0.01);
    return {minimum - span * padding_fraction, maximum + span * padding_fraction};
}

double fit_volume_maximum(std::span<const Market::Core::Series::Bar> bars) {
    if (bars.empty())
        return 1.0;

    return std::max(1.0, std::ranges::max(bars, {}, &Market::Core::Series::Bar::volume).volume);
}

std::chrono::seconds minimum_zoom_duration(Market::Core::Time::Frame timeframe, std::uint32_t visible_bar_count) {
    using enum Market::Core::Time::Unit;
    const auto count = static_cast<std::int64_t>(
        std::max<std::uint64_t>(1, static_cast<std::uint64_t>(timeframe.quantity) * visible_bar_count));
    switch (timeframe.unit) {
        case Minute:
            return std::chrono::minutes{count};
        case Hour:
            return std::chrono::hours{count};
        case Day:
            return std::chrono::days{count};
    }
    return std::chrono::seconds{1};
}

Market::Core::Time::Range zoom_time_range(Market::Core::Time::Range current, Market::Core::Time::UtcTimestamp anchor,
                                          double wheel_steps, std::chrono::seconds minimum_duration,
                                          Market::Core::Time::Range bounds) {
    if (current.empty() || bounds.empty())
        return current;

    const auto current_seconds = std::chrono::duration<double>(current.end - current.begin).count();
    const auto bounded_anchor = std::clamp(anchor, current.begin, current.end);
    const auto anchor_fraction =
        std::chrono::duration<double>(bounded_anchor - current.begin).count() / current_seconds;
    const auto bounds_seconds = std::chrono::duration<double>(bounds.end - bounds.begin).count();
    const auto minimum_seconds = std::min(static_cast<double>(minimum_duration.count()), bounds_seconds);
    const auto duration = std::clamp(current_seconds * std::pow(0.8, wheel_steps), minimum_seconds, bounds_seconds);
    auto begin = bounded_anchor - std::chrono::duration_cast<Market::Core::Time::UtcTimestamp::duration>(
                                      std::chrono::duration<double>{duration * anchor_fraction});
    auto end = begin + std::chrono::duration_cast<Market::Core::Time::UtcTimestamp::duration>(
                           std::chrono::duration<double>{duration});
    if (begin < bounds.begin) {
        end += bounds.begin - begin;
        begin = bounds.begin;
    }
    if (end > bounds.end) {
        begin -= end - bounds.end;
        end = bounds.end;
    }
    return {std::max(begin, bounds.begin), std::min(end, bounds.end)};
}

Market::Core::Time::Range pan_time_range(Market::Core::Time::Range current, double pixel_delta, double viewport_width,
                                         Market::Core::Time::Range bounds) {
    if (current.empty() || bounds.empty() || viewport_width <= 0.0)
        return current;

    const auto duration = current.end - current.begin;
    if (duration >= bounds.end - bounds.begin)
        return bounds;

    const auto shift =
        std::chrono::duration_cast<Market::Core::Time::UtcTimestamp::duration>(std::chrono::duration<double>{
            std::chrono::duration<double>(duration).count() * (-pixel_delta / viewport_width)});
    auto result = Market::Core::Time::Range{current.begin + shift, current.end + shift};
    if (result.begin < bounds.begin)
        result = {bounds.begin, bounds.begin + duration};
    if (result.end > bounds.end)
        result = {bounds.end - duration, bounds.end};

    return result;
}

CandleGeometry build_candles(std::span<const Market::Core::Series::Bar> bars, const CoordinateMapper& price_mapper,
                             const CoordinateMapper& volume_mapper) {
    CandleGeometry result;
    result.wicks.reserve(bars.size() * 2);
    result.bodies.reserve(bars.size() * 2);
    result.volume.reserve(bars.size() * 2);
    auto spacing = 10.0F;
    if (bars.size() > 1) {
        spacing = std::numeric_limits<float>::max();
        for (std::size_t index = 1; index < bars.size(); ++index) {
            const auto previous = price_mapper.map(bars[index - 1].open_time, bars[index - 1].close).x;
            const auto current = price_mapper.map(bars[index].open_time, bars[index].close).x;
            const auto distance = std::abs(current - previous);
            if (distance > 0.0F)
                spacing = std::min(spacing, distance);
        }
        if (spacing == std::numeric_limits<float>::max())
            spacing = 10.0F;
    }
    const auto half_width = std::clamp(spacing * 0.28F, 3.0F, 9.0F);
    for (const auto& bar : bars) {
        const auto high = price_mapper.map(bar.open_time, bar.high);
        const auto low = price_mapper.map(bar.open_time, bar.low);
        result.wicks.insert(result.wicks.end(), {high, low});
        const auto open = price_mapper.map(bar.open_time, bar.open);
        const auto close = price_mapper.map(bar.open_time, bar.close);
        const auto body_top = std::min(open.y, close.y);
        const auto body_bottom = std::max(body_top + 2.0F, std::max(open.y, close.y));
        result.bodies.push_back(
            {{open.x - half_width, body_top}, {open.x + half_width, body_bottom}, bar.close >= bar.open});
        const auto volume = volume_mapper.map(bar.open_time, bar.volume);
        const auto baseline = volume_mapper.map(bar.open_time, 0.0);
        result.volume.push_back({{high.x - half_width, std::min(volume.y, baseline.y)},
                                 {high.x + half_width, std::max(volume.y, baseline.y)},
                                 bar.close >= bar.open});
    }
    return result;
}

LineGeometry build_line(std::span<const Analysis::Core::Indicator::OutputSample> samples,
                        const CoordinateMapper& mapper) {
    LineGeometry result;
    result.points.reserve(samples.size());
    for (const auto& sample : samples)
        result.points.push_back(mapper.map(sample.timestamp, sample.value));
    return result;
}

BandGeometry build_band(std::span<const Analysis::Core::Indicator::OutputSample> upper,
                        std::span<const Analysis::Core::Indicator::OutputSample> lower,
                        const CoordinateMapper& mapper) {
    BandGeometry result;
    const auto count = std::min(upper.size(), lower.size());
    result.upper.reserve(count);
    result.lower.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        if (upper[index].timestamp != lower[index].timestamp)
            continue;
        result.upper.push_back(mapper.map(upper[index].timestamp, upper[index].value));
        result.lower.push_back(mapper.map(lower[index].timestamp, lower[index].value));
    }
    return result;
}

std::optional<HitResult> hit_test(std::span<const Market::Core::Series::Bar> bars, const CoordinateMapper& mapper,
                                  Point point, float maximum_distance) {
    std::optional<HitResult> result;
    float closest = maximum_distance;
    for (std::size_t index = 0; index < bars.size(); ++index) {
        const auto mapped = mapper.map(bars[index].open_time, bars[index].close);
        const auto distance = std::abs(mapped.x - point.x);
        if (distance <= closest) {
            closest = distance;
            result = HitResult{index, bars[index].open_time, bars[index].close};
        }
    }
    return result;
}

void RenderScheduler::request(RenderTask task) {
    const auto found =
        std::ranges::find_if(m_tasks, [&](const auto& queued) { return queued.chart_id == task.chart_id; });
    if (found == m_tasks.end()) {
        m_tasks.push_back(std::move(task));
        return;
    }

    if (task.chart_revision >= found->chart_revision) {
        found->chart_revision = task.chart_revision;
        found->reasons = found->reasons | task.reasons;
    }
}

std::vector<RenderTask> RenderScheduler::drain() {
    if (std::this_thread::get_id() != m_ui_thread)
        throw std::logic_error("Render tasks may only be drained on the owning UI/GL thread");

    return std::exchange(m_tasks, {});
}
} // namespace Didrachma::StockChart::Render
