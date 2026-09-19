#include "TestAssert.h"

#include <Didrachma/stockChart/render/State.h>
#include <tuple>

using namespace Didrachma::StockChart::Render;
using namespace Didrachma::Market::Core;

Time::UtcTimestamp at(int seconds) {
    return Time::UtcTimestamp{std::chrono::seconds{seconds}};
}

int test_coordinates_use_local_origin_and_round_trip() {
    CPPTEST_ASSERT(!mapping_is_valid({at(0), at(10)}, {0, 0}, {100, 100}));
    CPPTEST_ASSERT(!mapping_is_valid({at(0), at(0)}, {0, 1}, {100, 100}));
    CPPTEST_ASSERT(mapping_is_valid({at(0), at(10)}, {0, 1}, {100, 100}));

    CoordinateMapper mapper{{at(1000000000), at(1000000100)}, {10, 20}, {1000, 500}};
    const auto point = mapper.map(at(1000000025), 15);
    CPPTEST_ASSERT(point.x == 250 && point.y == 250 && mapper.origin() == at(1000000000));
    const auto [time, price] = mapper.unmap(point);
    CPPTEST_ASSERT(time == at(1000000025) && price == 15);
    return 0;
}

int test_clipping_and_geometry() {
    const auto columns = covering_pixel_columns(10.8F, 14.2F);
    CPPTEST_ASSERT(columns.first == 10 && columns.last_exclusive == 15);

    std::vector<Series::Bar> bars{
        {at(0), at(1), 1, 3, 0, 2, 10}, {at(10), at(11), 2, 4, 1, 3, 20}, {at(20), at(21), 3, 5, 2, 4, 30}};
    const auto clipped = visible_bars(bars, {at(10), at(20)});
    CPPTEST_ASSERT(clipped.size() == 1 && clipped[0].open_time == at(10));
    CoordinateMapper mapper{{at(0), at(30)}, {0, 6}, {300, 120}};
    CoordinateMapper volume_mapper{{at(0), at(30)}, {0, 40}, {300, 120}};
    const auto geometry = build_candles(clipped, mapper, volume_mapper);
    CPPTEST_ASSERT(geometry.wicks.size() == 2 && geometry.bodies.size() == 1 && geometry.volume.size() == 1);
    CPPTEST_ASSERT(geometry.bodies.front().rising && geometry.bodies.front().maximum.x == 103);
    CPPTEST_ASSERT(geometry.volume.front().minimum.y == 60);
    CPPTEST_ASSERT(geometry.volume.front().maximum.y == 120);

    std::vector<Series::Bar> bars_after_gap{
        {at(0), at(1), 1, 3, 0, 2, 10}, {at(300), at(301), 2, 4, 1, 3, 20}, {at(400), at(401), 3, 5, 2, 4, 30}};
    CoordinateMapper gap_mapper{{at(0), at(700)}, {0, 6}, {70, 120}};
    CoordinateMapper gap_volume_mapper{{at(0), at(700)}, {0, 40}, {70, 120}};
    const auto gap_geometry = build_candles(bars_after_gap, gap_mapper, gap_volume_mapper);
    CPPTEST_ASSERT(gap_geometry.bodies.front().maximum.x - gap_geometry.bodies.front().minimum.x == 6);
    CPPTEST_ASSERT(gap_geometry.volume.front().maximum.x - gap_geometry.volume.front().minimum.x == 6);
    return 0;
}

int test_linked_panes_and_shared_hover_mapping() {
    const auto panes = linked_pane_layout({800, 500}, 0.76F);
    CPPTEST_ASSERT(panes.price_height == 376 && panes.volume_top == 381 && panes.volume_height == 119);
    CPPTEST_ASSERT(panes.price_content_height == panes.price_height && panes.separate_height == 0);
    const auto minimum = linked_pane_layout({200, 120}, 0.99F);
    CPPTEST_ASSERT(minimum.price_height == 58 && minimum.volume_height == 57);
    const auto unsettled = linked_pane_layout({1, 1}, 0.76F);
    CPPTEST_ASSERT(unsettled.price_content_height == 0 && unsettled.volume_height == 0);
    CPPTEST_ASSERT(price_pane_ratio_from_splitter(90, {200, 120}) == 58.0F / 115.0F);
    CPPTEST_ASSERT(price_pane_ratio_from_splitter(20, {200, 500}) == 64.0F / 495.0F);
    CPPTEST_ASSERT(price_pane_ratio_from_splitter(430, {200, 500}) == 430.0F / 495.0F);
    CPPTEST_ASSERT(axis_label_left(100, 200, 150, 40) == 130);
    CPPTEST_ASSERT(axis_label_left(100, 115, 100, 132) == 100);
    CPPTEST_ASSERT(axis_label_left(100, 115, 115, 132) == 100);
    const auto with_indicator = with_separate_pane(panes, 0.7F);
    CPPTEST_ASSERT(with_indicator.price_content_height == 259);
    CPPTEST_ASSERT(with_indicator.separate_top == 0 && with_indicator.separate_height == 112);
    CPPTEST_ASSERT(with_indicator.separate_splitter_y == 112 && with_indicator.price_top == 117);
    CPPTEST_ASSERT(with_indicator.separate_height == 112);
    CPPTEST_ASSERT(separate_pane_ratio_from_splitter(100, with_indicator) == 271.0F / 371.0F);
    CPPTEST_ASSERT(pane_content_top(with_indicator.separate_top) == 24);
    CPPTEST_ASSERT(pane_content_top(with_indicator.price_top) == 141);
    CPPTEST_ASSERT(pane_content_top(with_indicator.volume_top) == 405);
    CPPTEST_ASSERT(pane_content_height(with_indicator.separate_height) == 88);
    CPPTEST_ASSERT(pane_content_height(with_indicator.price_content_height) == 235);
    CPPTEST_ASSERT(pane_content_height(with_indicator.volume_height) == 95);

    CoordinateMapper price{{at(0), at(100)}, {0, 10}, {800, panes.price_height}};
    CoordinateMapper volume{
        {at(0), at(100)}, {0, 1000}, {800, panes.volume_height}, static_cast<float>(panes.volume_top)};
    CPPTEST_ASSERT(price.map(at(25), 5).x == volume.map(at(25), 500).x);
    std::vector<Series::Bar> bars{{at(25), at(30), 1, 3, 0, 2, 10}};
    CPPTEST_ASSERT(hit_test(bars, price, {200, 100})->time == hit_test(bars, volume, {200, 450})->time);
    CPPTEST_ASSERT(!hit_test(bars, price, {400, 100}));
    return 0;
}

int test_indicator_geometry_and_hit_testing() {
    CoordinateMapper mapper{{at(0), at(30)}, {0, 6}, {300, 120}};
    using Sample = Didrachma::Analysis::Core::Indicator::OutputSample;
    std::vector<Sample> middle{{at(10), 2}, {at(20), 3}};
    std::vector<Sample> upper{{at(10), 4}, {at(20), 5}};
    std::vector<Sample> lower{{at(10), 1}, {at(20), 2}};
    CPPTEST_ASSERT(build_line(middle, mapper).points.size() == 2);
    CPPTEST_ASSERT(build_band(upper, lower, mapper).upper.size() == 2);

    std::vector<Series::Bar> bars{{at(10), at(11), 1, 3, 0, 2, 10}, {at(20), at(21), 2, 4, 1, 3, 20}};
    const auto hit = hit_test(bars, mapper, {102, 80});
    CPPTEST_ASSERT(hit && hit->index == 0 && hit->price == 2);
    CPPTEST_ASSERT(!hit_test(bars, mapper, {10, 80}));
    return 0;
}

int test_marker_placement_collision_selection_and_clipping() {
    namespace Condition = Didrachma::Analysis::Core::Condition;
    CoordinateMapper mapper{{at(0), at(30)}, {0, 20}, {300, 200}};
    std::vector<Series::Bar> bars{{at(10), at(11), 10, 14, 8, 12, 10}, {at(20), at(21), 12, 15, 9, 10, 20}};
    const Series::Key key{"fixture", "ABC", {1, Time::Unit::Minute}};
    std::vector<Condition::Event> events;
    for (const auto& [id, instance, direction] :
         std::vector<std::tuple<std::string, std::string, Condition::Direction>>{
             {"up", "one", Condition::Direction::Upward},
             {"down", "two", Condition::Direction::Downward},
             {"neutral", "one", Condition::Direction::Neutral}}) {
        Condition::Event event{id, "pattern", key, at(10)};
        event.chart_id = "chart";
        event.source_instance_id = instance;
        event.direction = direction;
        events.push_back(std::move(event));
    }
    Condition::Event outside{"outside", "pattern", key, at(40)};
    outside.chart_id = "chart";
    outside.source_instance_id = "one";
    events.push_back(std::move(outside));

    const auto first = build_markers(events, bars, mapper, {1, 1, 0, 1}, "chart", "one", "neutral");
    const auto second = build_markers(events, bars, mapper, {1, 0, 0, 1}, "chart", "two");
    CPPTEST_ASSERT(first.glyphs.size() == 2 && second.glyphs.size() == 1);
    CPPTEST_ASSERT(first.glyphs[0].center.y > mapper.map(at(10), 8).y);
    CPPTEST_ASSERT(second.glyphs[0].center.y < mapper.map(at(10), 14).y);
    CPPTEST_ASSERT(first.glyphs[1].selected && first.glyphs[1].size == 9);
    CPPTEST_ASSERT(first.glyphs[1].center.y != second.glyphs[0].center.y);
    return 0;
}

int test_event_selection_line_leaves_candle_gap() {
    CoordinateMapper mapper{{at(0), at(30)}, {0, 20}, {300, 200}};
    const std::vector<Series::Bar> bars{{at(10), at(11), 10, 14, 8, 12, 10}};
    const auto segments = build_event_selection_line(at(10), bars, mapper, 300);
    CPPTEST_ASSERT(segments.size() == 2);
    CPPTEST_ASSERT(segments[0].first.y == 0 && segments[0].second.y < mapper.map(at(10), 14).y);
    CPPTEST_ASSERT(segments[1].first.y > mapper.map(at(10), 8).y && segments[1].second.y == 300);
    CPPTEST_ASSERT(segments[0].first.x == mapper.map(at(10), 12).x);
    CPPTEST_ASSERT(build_event_selection_line(at(20), bars, mapper, 300).empty());
    return 0;
}

int test_time_zoom_and_visible_price_fit() {
    std::vector<Series::Bar> bars{{at(10), at(11), 10, 14, 8, 12, 10}, {at(20), at(21), 12, 20, 11, 18, 20}};
    const auto price = fit_price_range(bars);
    CPPTEST_ASSERT(price.minimum == 6.8 && price.maximum == 21.2);
    CPPTEST_ASSERT(fit_volume_maximum(std::span{bars}.first(1)) == 10);
    CPPTEST_ASSERT(fit_volume_maximum(std::span{bars}.last(1)) == 20);
    CPPTEST_ASSERT(fit_price_range(std::span{bars}.first(1)).maximum <
                   fit_price_range(std::span{bars}.last(1)).maximum);

    CPPTEST_ASSERT(minimum_zoom_duration({1, Time::Unit::Minute}) == std::chrono::minutes{5});
    CPPTEST_ASSERT(minimum_zoom_duration({10, Time::Unit::Minute}) == std::chrono::minutes{50});
    CPPTEST_ASSERT(minimum_zoom_duration({1, Time::Unit::Hour}) == std::chrono::hours{5});
    CPPTEST_ASSERT(minimum_zoom_duration({1, Time::Unit::Day}) == std::chrono::days{5});

    const auto zoomed = zoom_time_range({at(0), at(100)}, at(50), 1, std::chrono::seconds{20}, {at(0), at(200)});
    CPPTEST_ASSERT(zoomed.begin == at(10) && zoomed.end == at(90));
    const auto expanded = zoom_time_range(zoomed, at(50), -10, std::chrono::seconds{20}, {at(0), at(200)});
    CPPTEST_ASSERT(expanded.begin == at(0) && expanded.end == at(200));
    CPPTEST_ASSERT(zoom_time_range({at(10), at(10)}, at(10), 1, std::chrono::seconds{20}, {at(0), at(200)}).empty());
    const auto minute_zoom = zoom_time_range({at(0), at(86400)}, at(43200), 100,
                                             minimum_zoom_duration({1, Time::Unit::Minute}), {at(0), at(86400)});
    CPPTEST_ASSERT(minute_zoom.end - minute_zoom.begin == std::chrono::minutes{5});

    const auto panned = pan_time_range({at(50), at(150)}, 25, 100, {at(0), at(200)});
    CPPTEST_ASSERT(panned.begin == at(25) && panned.end == at(125));
    const auto clamped = pan_time_range(panned, 200, 100, {at(0), at(200)});
    CPPTEST_ASSERT(clamped.begin == at(0) && clamped.end == at(100));
    CPPTEST_ASSERT(clamped.end - clamped.begin == panned.end - panned.begin);
    CPPTEST_ASSERT(pan_time_range({at(0), at(300)}, 20, 100, {at(0), at(200)}).end == at(200));
    return 0;
}

int test_dirty_cache_and_scheduler() {
    InvalidationState state;
    state.consume();
    CPPTEST_ASSERT(!state.invalidate_data({at(0), at(10)}, {at(20), at(30)}, false) && !state.needs_render());
    CPPTEST_ASSERT(state.invalidate_data({at(0), at(10)}, {at(20), at(30)}, true) && state.needs_render());

    LayerCache cache;
    LayerRevision revision{1, 2, 3, {at(20), at(30)}, true};
    CPPTEST_ASSERT(cache.stale(revision) && cache.update(revision) && !cache.update(revision));

    RenderScheduler scheduler;
    scheduler.request({"chart", 1, DirtyReason::Data});
    scheduler.request({"chart", 2, DirtyReason::Style});
    const auto tasks = scheduler.drain();
    CPPTEST_ASSERT(tasks.size() == 1 && tasks[0].chart_revision == 2 && scheduler.drain().empty());
    return 0;
}

int main() {
    CPPTEST_RUN(test_coordinates_use_local_origin_and_round_trip);
    CPPTEST_RUN(test_clipping_and_geometry);
    CPPTEST_RUN(test_indicator_geometry_and_hit_testing);
    CPPTEST_RUN(test_marker_placement_collision_selection_and_clipping);
    CPPTEST_RUN(test_event_selection_line_leaves_candle_gap);
    CPPTEST_RUN(test_linked_panes_and_shared_hover_mapping);
    CPPTEST_RUN(test_time_zoom_and_visible_price_fit);
    CPPTEST_RUN(test_dirty_cache_and_scheduler);
    return 0;
}
