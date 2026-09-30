#include "IndicatorActions.h"

#include <Didrachma/stockChart/core/PatternProjection.h>

namespace Didrachma::Apps::Studio {
StockChart::Core::Profile capture_profile(const StockChart::Core::Document& document, std::string name) {
    return StockChart::Core::capture_profile(document, std::move(name));
}

std::string add_indicator(StockChart::Core::Document& document,
                          const Analysis::Core::Indicator::Definition& definition) {
    std::map<std::string, Analysis::Core::Indicator::ParameterValue> parameters;
    for (const auto& parameter : definition.parameters)
        parameters.emplace(parameter.id, parameter.default_value);

    const auto instance = document.add_indicator(definition.id, std::move(parameters));
    document.set_indicator_name(instance, definition.display_name);
    if (definition.capability == Analysis::Core::Indicator::Capability::AnalysisEvent) {
        StockChart::Core::reconcile_pattern_projection(document, definition, instance);
        return instance;
    }

    const auto pane =
        definition.pane == Analysis::Core::Indicator::PaneHint::Volume     ? StockChart::Core::LayerPane::Volume
        : definition.pane == Analysis::Core::Indicator::PaneHint::Separate ? StockChart::Core::LayerPane::Separate
                                                                           : StockChart::Core::LayerPane::Price;
    std::optional<std::string> upper;
    std::optional<std::string> lower;
    for (const auto& output : definition.outputs) {
        if (output.role == Analysis::Core::Indicator::OutputRole::UpperBand)
            upper = output.id;
        else if (output.role == Analysis::Core::Indicator::OutputRole::LowerBand)
            lower = output.id;
        else if (output.visual == Analysis::Core::Indicator::VisualKind::Line)
            document.add_layer(StockChart::Core::LayerKind::Line, {{instance, output.id}}, {}, pane);
        else if (output.visual == Analysis::Core::Indicator::VisualKind::Histogram)
            document.add_layer(StockChart::Core::LayerKind::Histogram, {{instance, output.id}}, {}, pane);
    }
    if (upper && lower)
        document.add_layer(StockChart::Core::LayerKind::Band, {{instance, *upper}, {instance, *lower}}, {}, pane);

    return instance;
}
} // namespace Didrachma::Apps::Studio
