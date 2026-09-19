#include "TestAssert.h"

#include <Didrachma/stockChart/core/Document.h>
#include <Didrachma/stockChart/core/PatternProjection.h>

using namespace Didrachma::StockChart::Core;
using namespace Didrachma::Market::Core;

Time::UtcTimestamp at(int seconds) {
    return Time::UtcTimestamp{std::chrono::seconds{seconds}};
}

Document chart(std::string id) {
    return {std::move(id), {"fake", "TEST", {1, Time::Unit::Day}}, {at(0), at(100)}};
}

int test_chart_identity_state_and_isolation() {
    auto first = chart("first");
    auto second = chart("second");
    CPPTEST_ASSERT(first.id() == "first" && first.series().instrument == "TEST");
    CPPTEST_ASSERT(first.auto_follow() && first.price_range_mode() == PriceRangeMode::Automatic);
    first.set_auto_follow(false);
    first.set_price_range_mode(PriceRangeMode::Manual);
    const auto first_id = first.add_indicator("sma", {{"period", std::int64_t{20}}});
    const auto second_id = second.add_indicator("sma", {{"period", std::int64_t{50}}});
    CPPTEST_ASSERT(first_id != second_id && first.indicators().size() == 1 && second.indicators().size() == 1);
    return 0;
}

int test_disable_retains_state_position_and_remove_cascades() {
    auto document = chart("chart");
    const auto id = document.add_indicator("bbands", {{"period", std::int64_t{20}}});
    document.find_indicator(id)->cached_result = Didrachma::Analysis::Core::Indicator::Result{{}, 4, 7};
    const auto band = document.add_layer(LayerKind::Band, {{id, "upper"}, {id, "lower"}});
    document.add_layer(LayerKind::Line, {{id, "middle"}});
    CPPTEST_ASSERT(document.set_indicator_enabled(id, false));
    CPPTEST_ASSERT(!document.indicators()[0].instance.enabled);
    CPPTEST_ASSERT(std::get<std::int64_t>(document.indicators()[0].instance.parameters.at("period")) == 20);
    CPPTEST_ASSERT(document.indicators()[0].cached_result->revision == 7 && document.layers()[0].id == band);
    CPPTEST_ASSERT(document.set_indicator_enabled(id, true) && document.indicators()[0].instance.enabled);
    CPPTEST_ASSERT(document.remove_indicator(id) && document.indicators().empty() && document.layers().empty());
    return 0;
}

int test_style_revision_and_navigation() {
    auto document = chart("chart");
    const auto id = document.add_indicator("sma", {});
    const auto layer_id = document.add_layer(LayerKind::Line, {{id, "value"}});
    auto style = document.layers()[0].style;
    CPPTEST_ASSERT(document.set_layer_style(layer_id, style) && document.layers()[0].style_revision == 1);
    style.color = {1, 0, 0, 1};
    style.line_width = 2;
    CPPTEST_ASSERT(document.set_layer_style(layer_id, style) && document.layers()[0].style_revision == 2);
    CPPTEST_ASSERT(document.dispatch(NavigateViewport{{at(20), at(80)}})[0].kind ==
                   NavigationEventKind::ViewportChanged);
    document.dispatch(SelectTimestamp{at(30)});
    document.dispatch(SelectRange{Time::Range{at(30), at(40)}});
    document.dispatch(SelectAnalysisEvent{std::string{"event-1"}});
    CPPTEST_ASSERT(document.visible_range().begin == at(20) && *document.selected_timestamp() == at(30));
    CPPTEST_ASSERT(document.selected_range()->end == at(40) && *document.selected_event_id() == "event-1");
    return 0;
}

int test_parameter_edit_and_reorder_are_chart_local() {
    auto document = chart("chart");
    const auto first = document.add_indicator("sma", {{"period", std::int64_t{20}}});
    const auto second = document.add_indicator("sma", {{"period", std::int64_t{50}}});
    CPPTEST_ASSERT(document.set_indicator_name(first, "SMA-20"));
    CPPTEST_ASSERT(document.find_indicator(first)->name == "SMA-20");
    CPPTEST_ASSERT(!document.set_indicator_name(first, ""));
    document.find_indicator(first)->cached_result = Didrachma::Analysis::Core::Indicator::Result{{}, 1, 1};
    CPPTEST_ASSERT(document.move_indicator(second, 0) && document.indicators()[0].instance.id == second);
    CPPTEST_ASSERT(document.set_indicator_parameters(first, {{"period", std::int64_t{30}}}));
    CPPTEST_ASSERT(!document.find_indicator(first)->cached_result.has_value());
    CPPTEST_ASSERT(std::get<std::int64_t>(document.find_indicator(second)->instance.parameters.at("period")) == 50);
    return 0;
}

int test_standalone_indicator_selection_tracks_instances() {
    auto document = chart("chart");
    const auto first = document.add_indicator("trange", {});
    const auto second = document.add_indicator("atr", {{"period", std::int64_t{14}}});
    const auto third = document.add_indicator("atr", {{"period", std::int64_t{30}}});
    document.add_layer(LayerKind::Line, {{first, "value"}}, {}, LayerPane::Separate);
    document.add_layer(LayerKind::Line, {{second, "value"}}, {}, LayerPane::Separate);
    document.add_layer(LayerKind::Line, {{third, "value"}}, {}, LayerPane::Separate);

    CPPTEST_ASSERT(document.standalone_indicator_ids() == std::vector<std::string>({first, second, third}));
    CPPTEST_ASSERT(document.selected_standalone_indicator_id() == first);
    CPPTEST_ASSERT(document.select_standalone_indicator(second));
    CPPTEST_ASSERT(document.selected_standalone_indicator_id() == second);
    CPPTEST_ASSERT(document.remove_indicator(second));
    CPPTEST_ASSERT(document.selected_standalone_indicator_id() == first);
    CPPTEST_ASSERT(document.set_indicator_enabled(first, false));
    CPPTEST_ASSERT(document.selected_standalone_indicator_id() == third);
    CPPTEST_ASSERT(document.set_indicator_enabled(third, false));
    CPPTEST_ASSERT(!document.selected_standalone_indicator_id());
    CPPTEST_ASSERT(document.standalone_indicator_ids().empty());
    return 0;
}

int test_pattern_projection_replaces_study_pane_with_one_price_marker() {
    namespace Indicator = Didrachma::Analysis::Core::Indicator;
    auto document = chart("chart");
    const auto instance = document.add_indicator("cdldoji", {});
    document.add_layer(LayerKind::Line, {{instance, "value"}}, {}, LayerPane::Separate);
    Indicator::Definition definition{"cdldoji", "Doji", {}, {{"value", "Integer", Indicator::VisualKind::Marker}}};
    definition.capability = Indicator::Capability::AnalysisEvent;

    CPPTEST_ASSERT(reconcile_pattern_projection(document, definition, instance));
    CPPTEST_ASSERT(document.layers().size() == 1);
    CPPTEST_ASSERT(document.layers()[0].kind == LayerKind::Marker && document.layers()[0].pane == LayerPane::Price);
    CPPTEST_ASSERT(document.layers()[0].outputs[0].instance_id == instance);
    CPPTEST_ASSERT(!document.selected_standalone_indicator_id());
    CPPTEST_ASSERT(!reconcile_pattern_projection(document, definition, instance));
    return 0;
}

int main() {
    CPPTEST_RUN(test_chart_identity_state_and_isolation);
    CPPTEST_RUN(test_disable_retains_state_position_and_remove_cascades);
    CPPTEST_RUN(test_style_revision_and_navigation);
    CPPTEST_RUN(test_parameter_edit_and_reorder_are_chart_local);
    CPPTEST_RUN(test_standalone_indicator_selection_tracks_instances);
    CPPTEST_RUN(test_pattern_projection_replaces_study_pane_with_one_price_marker);
    return 0;
}
