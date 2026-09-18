#include "TestAssert.h"

#include <Didrachma/analysis/adapters/talib/Analyzer.h>
#include <Didrachma/studio/core/Events.h>
#include <algorithm>
#include <cmath>

using namespace Didrachma;
using namespace Didrachma::Analysis::Core::Condition;
using Timestamp = Market::Core::Time::UtcTimestamp;

Timestamp at(int value) {
    return Timestamp{std::chrono::seconds{value}};
}

Event event(std::string id, std::string condition, std::string instrument, int begin, std::optional<int> end = {}) {
    return {std::move(id),
            std::move(condition),
            {"test", std::move(instrument), {1, Market::Core::Time::Unit::Day}},
            at(begin),
            end ? std::optional{at(*end)} : std::nullopt,
            Direction::Upward,
            Severity::Attention,
            "Title",
            "Reason",
            {{"value", 1}}};
}

int test_sort_filter_duplicate_suppression_and_span_pairing() {
    Studio::Core::EventList list;
    const std::vector first{event("one", "cross", "ABC", 10), event("two", "span", "ABC", 20),
                            event("three", "cross", "XYZ", 30)};
    list.publish(first);
    const std::vector completed{event("two", "span", "ABC", 20, 40), event("one", "cross", "ABC", 10)};
    list.publish(completed);
    CPPTEST_ASSERT(list.events().size() == 3 && list.events()[0].id == "three");
    CPPTEST_ASSERT(list.find("two")->end == at(40));
    const auto filtered = list.filtered({"ABC", "cross"});
    CPPTEST_ASSERT(filtered.size() == 1 && filtered[0]->id == "one");
    return 0;
}

int test_event_navigation_and_missing_history_request() {
    StockChart::Core::Document document{"chart", {"test", "ABC", {1, Market::Core::Time::Unit::Day}}, {at(0), at(100)}};
    const auto span = event("span", "condition", "ABC", 190, 230);
    const auto navigation = Studio::Core::navigate_to_event(document, span, Market::Core::Time::Range{at(0), at(100)});
    CPPTEST_ASSERT(document.selected_event_id() == "span" && document.selected_range().has_value());
    CPPTEST_ASSERT(document.visible_range().begin <= at(190) && document.visible_range().end >= at(230));
    CPPTEST_ASSERT(navigation.missing_history &&
                   navigation.missing_history->range.begin == navigation.revealed_range.begin);

    const auto point = event("point", "condition", "ABC", 50);
    const auto available =
        Studio::Core::navigate_to_event(document, point, Market::Core::Time::Range{at(-100), at(300)});
    CPPTEST_ASSERT(document.selected_timestamp() == at(50) && !available.missing_history);
    return 0;
}

int test_event_list_growth_preserves_all_unique_events() {
    Studio::Core::EventList list;
    std::vector<Event> events;
    events.reserve(2000);
    for (int index = 0; index < 2000; ++index)
        events.push_back(event("event-" + std::to_string(index), "growth", "ABC", index));
    list.publish(events);

    CPPTEST_ASSERT(list.events().size() == 2000);
    CPPTEST_ASSERT(list.events().front().start == at(1999) && list.events().back().start == at(0));
    return 0;
}

int test_chart_and_indicator_scoped_replacement() {
    Studio::Core::EventList list;
    const std::vector first{event("same", "cross", "ABC", 10)};
    const std::vector second{event("same", "cross", "ABC", 20)};
    list.replace("chart-1", "sma-1", "SMA-50", "sma", {{"period", "50"}}, first);
    list.replace("chart-2", "sma-2", "SMA-20", "sma", {{"period", "20"}}, second);
    CPPTEST_ASSERT(list.filtered({"ABC", std::nullopt, "chart-1"}).size() == 1);
    CPPTEST_ASSERT(list.filtered({"ABC", std::nullopt, "chart-2"})[0]->source_name == "SMA-20");
    CPPTEST_ASSERT(list.filtered({"ABC", std::nullopt, "chart-2"})[0]->source_parameters.at("period") == "20");
    CPPTEST_ASSERT(list.filtered({"ABC", std::nullopt, "chart-2"})[0]->source_definition_id == "sma");
    list.replace("chart-1", "sma-1", "SMA-50", "sma", {{"period", "50"}}, {});
    CPPTEST_ASSERT(list.filtered({"ABC", std::nullopt, "chart-1"}).empty());
    CPPTEST_ASSERT(list.filtered({"ABC", std::nullopt, "chart-2"}).size() == 1);
    return 0;
}

int test_configured_instances_drive_events_independently_of_layers() {
    StockChart::Core::Document document{"chart", {"test", "ABC", {1, Market::Core::Time::Unit::Day}}, {at(0), at(100)}};
    const auto sma20 = document.add_indicator("sma", {{"period", std::int64_t{20}}});
    const auto sma30 = document.add_indicator("sma", {{"period", std::int64_t{30}}});
    document.set_indicator_name(sma20, "Duplicate");
    document.set_indicator_name(sma30, "Duplicate");
    document.add_layer(StockChart::Core::LayerKind::Line, {{sma20, "value"}}, {{1, 1, 1, 1}, false, 1, 0.2F});
    const std::vector<Market::Core::Series::Bar> bars{{at(10), at(11), 9, 10, 8, 9, 1},
                                                      {at(20), at(21), 11, 12, 10, 11, 1}};
    const std::vector<Analysis::Core::Indicator::OutputSample> average20{{at(10), 10}, {at(20), 10}};
    const std::vector<Analysis::Core::Indicator::OutputSample> average30{{at(10), 8}, {at(20), 12}};
    document.find_indicator(sma20)->cached_result = Analysis::Core::Indicator::Result{
        {{"value", average20}}, 1, 1, Analysis::Core::Indicator::CalculationState::Ready};
    document.find_indicator(sma30)->cached_result = Analysis::Core::Indicator::Result{
        {{"value", average30}}, 1, 1, Analysis::Core::Indicator::CalculationState::Ready};

    Studio::Core::EventList list;
    Studio::Core::ConditionBindingRegistry registry;
    const std::vector<Analysis::Core::Indicator::Definition> definitions{
        {"sma",
         "SMA",
         {},
         {{"value", "SMA", Analysis::Core::Indicator::VisualKind::Line,
           Analysis::Core::Indicator::OutputRole::PriceReference}}}};
    registry.evaluate(document, bars, list, definitions);
    CPPTEST_ASSERT(list.events().size() == 2);
    CPPTEST_ASSERT(list.events()[0].source_name == "Duplicate");
    CPPTEST_ASSERT(list.events()[0].source_parameters.at("period") != list.events()[1].source_parameters.at("period"));

    document.set_indicator_enabled(sma20, false);
    registry.evaluate(document, bars, list, definitions);
    CPPTEST_ASSERT(list.events().size() == 1 && list.events()[0].source_instance_id == sma30);
    document.set_indicator_enabled(sma20, true);
    document.set_indicator_parameters(sma20, {{"period", std::int64_t{10}}});
    document.find_indicator(sma20)->cached_result = Analysis::Core::Indicator::Result{
        {{"value", average20}}, 2, 2, Analysis::Core::Indicator::CalculationState::Ready};
    registry.evaluate(document, bars, list, definitions);
    CPPTEST_ASSERT(list.events().size() == 2);
    CPPTEST_ASSERT(std::any_of(list.events().begin(), list.events().end(),
                               [](const auto& item) { return item.source_parameters.at("period") == "10"; }));
    CPPTEST_ASSERT(registry.set_enabled("price-cross", false));
    registry.evaluate(document, bars, list, definitions);
    CPPTEST_ASSERT(list.events().empty());
    CPPTEST_ASSERT(registry.set_enabled("price-cross", true));
    document.remove_indicator(sma30);
    registry.evaluate(document, bars, list, definitions);
    CPPTEST_ASSERT(list.events().size() == 1 && list.events()[0].source_instance_id == sma20);
    return 0;
}

int test_talib_moving_average_instances_produce_metadata_driven_events() {
    StockChart::Core::Document document{"chart", {"test", "ABC", {1, Market::Core::Time::Unit::Day}}, {at(0), at(100)}};
    const auto wma = document.add_indicator("wma", {{"period", std::int64_t{3}}});
    const auto trima = document.add_indicator("trima", {{"period", std::int64_t{4}}});
    document.set_indicator_name(wma, "WMA-3");
    document.set_indicator_name(trima, "TRIMA-4");
    document.add_layer(StockChart::Core::LayerKind::Line, {{wma, "value"}}, {{1, 1, 1, 1}, false, 1, 0.2F});

    std::vector<Market::Core::Series::Bar> bars;
    for (int index = 0; index < 40; ++index) {
        const auto close = 20.0 + std::sin(static_cast<double>(index) * 0.8) * 5.0;
        bars.push_back({at(index), at(index + 1), close, close + 1.0, close - 1.0, close, 100.0});
    }
    Analysis::Adapters::TaLib::Analyzer analyzer;
    for (const auto& entry : document.indicators())
        document.find_indicator(entry.instance.id)->cached_result =
            analyzer.calculate({entry.instance, bars, 1, std::nullopt}).result;

    Studio::Core::EventList events;
    Studio::Core::ConditionBindingRegistry registry;
    const auto catalog = analyzer.catalog();
    registry.evaluate(document, bars, events, catalog);
    CPPTEST_ASSERT(!events.events().empty());
    CPPTEST_ASSERT(std::ranges::any_of(events.events(), [&](const auto& item) {
        return item.source_instance_id == wma && item.source_definition_id == "wma" &&
               item.source_parameters.at("period") == "3";
    }));
    CPPTEST_ASSERT(std::ranges::any_of(events.events(), [&](const auto& item) {
        return item.source_instance_id == trima && item.source_definition_id == "trima" &&
               item.source_parameters.at("period") == "4";
    }));
    return 0;
}

int main() {
    CPPTEST_RUN(test_sort_filter_duplicate_suppression_and_span_pairing);
    CPPTEST_RUN(test_event_navigation_and_missing_history_request);
    CPPTEST_RUN(test_event_list_growth_preserves_all_unique_events);
    CPPTEST_RUN(test_chart_and_indicator_scoped_replacement);
    CPPTEST_RUN(test_configured_instances_drive_events_independently_of_layers);
    CPPTEST_RUN(test_talib_moving_average_instances_produce_metadata_driven_events);
    return 0;
}
