#include "ChartView.h"

#include <algorithm>
#include <functional>
#include <limits>

namespace Didrachma::Apps::Studio {
namespace Intern {
namespace Indicator = Analysis::Core::Indicator;
using Sample = Indicator::OutputSample;

struct LineOutput {
    std::vector<Sample> samples;
    StockChart::Core::VisualStyle style;
    StockChart::Core::LayerPane pane;
};

struct BandOutput {
    std::vector<Sample> upper;
    std::vector<Sample> lower;
    StockChart::Core::VisualStyle style;
};

StockChart::Render::PriceRange fit_volume_and_samples(double volume_maximum, std::span<const LineOutput> line_outputs) {
    auto minimum = 0.0;
    auto maximum = volume_maximum;
    for (const auto& line : line_outputs)
        if (line.pane == StockChart::Core::LayerPane::Volume)
            for (const auto& sample : line.samples) {
                minimum = std::min(minimum, sample.value);
                maximum = std::max(maximum, sample.value);
            }

    const auto extent = std::max(maximum - minimum, 1.0);
    if (minimum >= 0.0)
        return {0.0, maximum + extent * 0.05};

    return {minimum - extent * 0.05, maximum + extent * 0.05};
}

const std::vector<Sample>* output(const StockChart::Core::IndicatorEntry& entry, const std::string& id) {
    if (!entry.cached_result || entry.cached_result->state != Indicator::CalculationState::Ready)
        return nullptr;

    const auto found = std::ranges::find(entry.cached_result->outputs, id, &Indicator::OutputSeries::output_id);
    return found == entry.cached_result->outputs.end() ? nullptr : &found->samples;
}

void update_events(const ChartView& view, const StockChart::Core::Document& document,
                   Didrachma::Studio::Core::EventList& events) {
    static const Didrachma::Studio::Core::ConditionBindingRegistry registry;
    static const auto definitions = Analysis::Adapters::TaLib::Analyzer{}.catalog();
    registry.evaluate(document, view.bars, events, definitions);
}
} // namespace Intern

std::string timeframe_label(Market::Core::Time::Frame timeframe) {
    const auto quantity = std::to_string(timeframe.quantity);
    switch (timeframe.unit) {
        case Market::Core::Time::Unit::Minute:
            return quantity + " Minute";
        case Market::Core::Time::Unit::Hour:
            return quantity + " Hour";
        case Market::Core::Time::Unit::Day:
            return quantity + " Day";
    }
    return quantity;
}

void update_geometry(ChartView& view, StockChart::Core::Document& document,
                     Analysis::Adapters::TaLib::Analyzer& analyzer, Didrachma::Studio::Core::EventList* events) {
    using namespace StockChart::Render;
    const auto size = view.size;
    if (size.width <= 0 || size.height <= 0) {
        view.has_visible_geometry = false;
        return;
    }

    const auto visible = visible_bars(view.bars, view.visible_range);
    view.canvas->set_bar_counts(visible.size(), view.bars.size());
    if (visible.empty()) {
        if (events)
            events->clear_chart(document.id());
        view.has_visible_geometry = false;
        view.price_range = {0.0, 1.0};
        view.volume_range = {0.0, 1.0};
        view.separate_range = {0.0, 1.0};
        view.canvas->set_geometry({});
        view.canvas->set_indicators({}, {});
        return;
    }
    view.has_visible_geometry = true;

    for (const auto& item : document.indicators()) {
        const auto outcome = analyzer.calculate({item.instance, view.bars, 1, {}});
        document.find_indicator(item.instance.id)->cached_result = outcome.result;
    }
    if (events)
        Intern::update_events(view, document, *events);

    std::vector<Intern::LineOutput> line_outputs;
    std::vector<Intern::BandOutput> band_outputs;
    const auto& selected_standalone = document.selected_standalone_indicator_id();
    for (const auto& layer : document.layers()) {
        if (!layer.style.visible || layer.outputs.empty())
            continue;

        const auto* entry = document.find_indicator(layer.outputs.front().instance_id);
        if (!entry || !entry->instance.enabled)
            continue;

        if (layer.pane == StockChart::Core::LayerPane::Separate &&
            (!selected_standalone || entry->instance.id != *selected_standalone))
            continue;

        if (layer.kind == StockChart::Core::LayerKind::Line || layer.kind == StockChart::Core::LayerKind::Histogram) {
            if (const auto* samples = Intern::output(*entry, layer.outputs.front().output_id))
                line_outputs.push_back({*samples, layer.style, layer.pane});
        } else if (layer.kind == StockChart::Core::LayerKind::Band && layer.outputs.size() == 2) {
            const auto* upper = Intern::output(*entry, layer.outputs[0].output_id);
            const auto* lower = Intern::output(*entry, layer.outputs[1].output_id);
            if (upper && lower)
                band_outputs.push_back({*upper, *lower, layer.style});
        }
    }
    const auto in_view = [&](const Intern::Sample& sample) {
        return sample.timestamp >= view.visible_range.begin && sample.timestamp < view.visible_range.end;
    };
    view.price_range = fit_price_range(visible);
    const auto clip_and_fit = [&](std::vector<Intern::Sample>& samples) {
        std::erase_if(samples, std::not_fn(in_view));
        for (const auto& sample : samples) {
            view.price_range.minimum = std::min(view.price_range.minimum, sample.value);
            view.price_range.maximum = std::max(view.price_range.maximum, sample.value);
        }
    };
    for (auto& line : line_outputs) {
        std::erase_if(line.samples, std::not_fn(in_view));
        if (line.pane == StockChart::Core::LayerPane::Price)
            for (const auto& sample : line.samples) {
                view.price_range.minimum = std::min(view.price_range.minimum, sample.value);
                view.price_range.maximum = std::max(view.price_range.maximum, sample.value);
            }
    }
    for (auto& band : band_outputs) {
        clip_and_fit(band.upper);
        clip_and_fit(band.lower);
    }

    const auto volume_maximum = fit_volume_maximum(visible);
    view.volume_range = Intern::fit_volume_and_samples(volume_maximum, line_outputs);
    const bool has_separate = std::ranges::any_of(line_outputs, [](const auto& line) {
        return line.pane == StockChart::Core::LayerPane::Separate && !line.samples.empty();
    });
    constexpr int value_axis_width = 72;
    view.plot_width = std::max(1, size.width - value_axis_width);
    auto panes = linked_pane_layout(size, document.price_pane_ratio());
    panes.plot_width = view.plot_width;
    if (has_separate)
        panes = with_separate_pane(panes, document.separate_pane_ratio());
    view.panes = panes;
    view.canvas->set_pane_layout(panes);
    if (panes.price_content_height <= 0 || panes.volume_height <= 0 || (has_separate && panes.separate_height <= 0)) {
        view.has_visible_geometry = false;
        view.canvas->set_geometry({});
        view.canvas->set_indicators({}, {});
        return;
    }
    const Size price_size{view.plot_width, pane_content_height(panes.price_content_height)};
    CoordinateMapper mapper{view.visible_range, view.price_range, price_size,
                            static_cast<float>(pane_content_top(panes.price_top))};
    view.separate_range = {0.0, 1.0};
    if (has_separate) {
        view.separate_range = {std::numeric_limits<double>::max(), std::numeric_limits<double>::lowest()};
        for (const auto& line : line_outputs)
            if (line.pane == StockChart::Core::LayerPane::Separate)
                for (const auto& sample : line.samples) {
                    view.separate_range.minimum = std::min(view.separate_range.minimum, sample.value);
                    view.separate_range.maximum = std::max(view.separate_range.maximum, sample.value);
                }
        if (!(view.separate_range.minimum < view.separate_range.maximum))
            view.separate_range = {view.separate_range.minimum - 1.0, view.separate_range.maximum + 1.0};
    }
    const Size separate_size{view.plot_width, has_separate ? pane_content_height(panes.separate_height) : 1};
    const CoordinateMapper separate_mapper{view.visible_range, view.separate_range, separate_size,
                                           static_cast<float>(pane_content_top(panes.separate_top))};
    const Size volume_size{view.plot_width, pane_content_height(panes.volume_height)};
    const CoordinateMapper volume_mapper{view.visible_range, view.volume_range, volume_size,
                                         static_cast<float>(pane_content_top(panes.volume_top))};
    view.canvas->set_geometry(build_candles(visible, mapper, volume_mapper));
    std::vector<StyledLineGeometry> lines;
    for (const auto& line : line_outputs) {
        const auto& selected_mapper = line.pane == StockChart::Core::LayerPane::Separate ? separate_mapper
                                      : line.pane == StockChart::Core::LayerPane::Volume ? volume_mapper
                                                                                         : mapper;
        lines.push_back({build_line(line.samples, selected_mapper), line.style.color, line.style.line_width});
    }
    std::vector<StyledBandGeometry> bands;
    for (const auto& band : band_outputs)
        bands.push_back({build_band(band.upper, band.lower, mapper), band.style.color, band.style.line_width,
                         band.style.band_fill_opacity});
    view.canvas->set_indicators(std::move(lines), std::move(bands));
}
} // namespace Didrachma::Apps::Studio
