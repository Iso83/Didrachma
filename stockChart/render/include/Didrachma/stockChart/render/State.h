#pragma once

#include <Didrachma/analysis/core/indicator/Result.h>
#include <Didrachma/market/core/series/Bar.h>
#include <Didrachma/market/core/time/Frame.h>
#include <Didrachma/stockChart/core/Layer.h>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace Didrachma::StockChart::Render {
struct Size {
    int width{}, height{};
    friend bool operator==(const Size&, const Size&) = default;
};

struct Point {
    float x{}, y{};
};

struct PixelColumnRange {
    int first{}, last_exclusive{};
};

PixelColumnRange covering_pixel_columns(float first, float second);

struct Rectangle {
    Point minimum;
    Point maximum;
    bool rising{};
};

struct PriceRange {
    double minimum{}, maximum{};
};

struct PaneLayout {
    int price_height{};
    int splitter_y{};
    int volume_top{};
    int volume_height{};
    int price_top{};
    int price_content_height{};
    int separate_splitter_y{};
    int separate_top{};
    int separate_height{};
    int plot_width{};
};

inline constexpr int pane_header_height = 24;

[[nodiscard]] int pane_content_top(int pane_top);
[[nodiscard]] int pane_content_height(int pane_height);

[[nodiscard]] PaneLayout linked_pane_layout(Size size, float price_ratio, int splitter_height = 5,
                                            int minimum_pane_height = 64);
[[nodiscard]] float price_pane_ratio_from_splitter(float pointer_y, Size size, int splitter_height = 5,
                                                   int minimum_pane_height = 64);
[[nodiscard]] PaneLayout with_separate_pane(PaneLayout panes, float price_ratio, int splitter_height = 5,
                                            int minimum_pane_height = 64);
[[nodiscard]] float separate_pane_ratio_from_splitter(float pointer_y, PaneLayout panes, int splitter_height = 5,
                                                      int minimum_pane_height = 64);
[[nodiscard]] float axis_label_left(float minimum_x, float maximum_x, float center_x, float label_width);

[[nodiscard]] bool mapping_is_valid(Market::Core::Time::Range time, PriceRange price, Size size);

class CoordinateMapper {
    Market::Core::Time::Range m_time;
    PriceRange m_price;
    Size m_size;
    float m_y_offset{};

public:
    CoordinateMapper(Market::Core::Time::Range time, PriceRange price, Size size, float y_offset = 0.0F);

    [[nodiscard]] Point map(Market::Core::Time::UtcTimestamp time, double price) const;
    [[nodiscard]] std::pair<Market::Core::Time::UtcTimestamp, double> unmap(Point point) const;
    [[nodiscard]] Market::Core::Time::UtcTimestamp origin() const {
        return m_time.begin;
    }
    [[nodiscard]] Size size() const {
        return m_size;
    }
};

enum class DirtyReason : std::uint32_t {
    None = 0,
    Data = 1,
    Viewport = 2,
    Size = 4,
    Style = 8,
    Visibility = 16,
    Selection = 32
};

constexpr DirtyReason operator|(DirtyReason left, DirtyReason right) {
    return static_cast<DirtyReason>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

struct LayerRevision {
    std::uint64_t model{}, result{}, style{};
    Market::Core::Time::Range visible_range;
    bool visible{true};

    friend bool operator==(const LayerRevision& left, const LayerRevision& right) {
        return left.model == right.model && left.result == right.result && left.style == right.style &&
               left.visible_range.begin == right.visible_range.begin &&
               left.visible_range.end == right.visible_range.end && left.visible == right.visible;
    }
};

struct RenderCounters {
    std::uint64_t canvas_renders{}, layer_rebuilds{}, uploaded_bytes{}, uploaded_elements{}, skipped_frames{};
    std::uint64_t viewport_bars{}, loaded_bars{};
    std::chrono::nanoseconds render_time{};
    std::chrono::nanoseconds last_render_time{};
};

class InvalidationState {
    DirtyReason m_dirty{DirtyReason::Viewport | DirtyReason::Size};

public:
    [[nodiscard]] DirtyReason dirty() const {
        return m_dirty;
    }
    [[nodiscard]] bool needs_render() const {
        return m_dirty != DirtyReason::None;
    }
    void invalidate(DirtyReason reason) {
        m_dirty = m_dirty | reason;
    }
    DirtyReason consume() {
        auto result = m_dirty;
        m_dirty = DirtyReason::None;
        return result;
    }
    bool invalidate_data(const Market::Core::Time::Range& dirty, const Market::Core::Time::Range& visible,
                         bool auto_follow);
};

class LayerCache {
    std::optional<LayerRevision> m_revision;

public:
    [[nodiscard]] bool stale(const LayerRevision& revision) const {
        return m_revision != revision;
    }
    bool update(const LayerRevision& revision) {
        const bool changed = stale(revision);
        m_revision = revision;
        return changed;
    }
};

struct CandleGeometry {
    std::vector<Point> wicks;
    std::vector<Rectangle> bodies;
    std::vector<Rectangle> volume;
};

struct LineGeometry {
    std::vector<Point> points;
};

struct BandGeometry {
    std::vector<Point> upper;
    std::vector<Point> lower;
};

struct StyledLineGeometry {
    LineGeometry geometry;
    Core::Color color;
    float width{1.0F};
};

struct StyledBandGeometry {
    BandGeometry geometry;
    Core::Color color;
    float width{1.0F};
    float fill_opacity{0.2F};
};

std::span<const Market::Core::Series::Bar> visible_bars(std::span<const Market::Core::Series::Bar> bars,
                                                        Market::Core::Time::Range range);
PriceRange fit_price_range(std::span<const Market::Core::Series::Bar> bars, double padding_fraction = 0.1);
double fit_volume_maximum(std::span<const Market::Core::Series::Bar> bars);
std::chrono::seconds minimum_zoom_duration(Market::Core::Time::Frame timeframe, std::uint32_t visible_bar_count = 5);
Market::Core::Time::Range zoom_time_range(Market::Core::Time::Range current, Market::Core::Time::UtcTimestamp anchor,
                                          double wheel_steps, std::chrono::seconds minimum_duration,
                                          Market::Core::Time::Range bounds);
Market::Core::Time::Range pan_time_range(Market::Core::Time::Range current, double pixel_delta, double viewport_width,
                                         Market::Core::Time::Range bounds);
CandleGeometry build_candles(std::span<const Market::Core::Series::Bar> bars, const CoordinateMapper& price_mapper,
                             const CoordinateMapper& volume_mapper);
LineGeometry build_line(std::span<const Analysis::Core::Indicator::OutputSample> samples,
                        const CoordinateMapper& mapper);
BandGeometry build_band(std::span<const Analysis::Core::Indicator::OutputSample> upper,
                        std::span<const Analysis::Core::Indicator::OutputSample> lower, const CoordinateMapper& mapper);

struct HitResult {
    std::size_t index{};
    Market::Core::Time::UtcTimestamp time;
    double price{};
};

std::optional<HitResult> hit_test(std::span<const Market::Core::Series::Bar> bars, const CoordinateMapper& mapper,
                                  Point point, float maximum_distance = 12.0F);

struct RenderTask {
    std::string chart_id;
    std::uint64_t chart_revision{};
    DirtyReason reasons{DirtyReason::None};
};

class RenderScheduler {
    std::vector<RenderTask> m_tasks;
    std::thread::id m_ui_thread{std::this_thread::get_id()};

public:
    void request(RenderTask task);
    [[nodiscard]] std::vector<RenderTask> drain();
};
} // namespace Didrachma::StockChart::Render
