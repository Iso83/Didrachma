#include <Didrachma/stockChart/core/PatternProjection.h>

namespace Didrachma::StockChart::Core {
bool reconcile_pattern_projection(Document& document, const Analysis::Core::Indicator::Definition& definition,
                                  const std::string& instance_id) {
    using namespace Analysis::Core::Indicator;
    if (definition.capability != Capability::AnalysisEvent)
        return false;

    const auto marker = std::ranges::find(definition.outputs, VisualKind::Marker, &OutputDefinition::visual);
    if (marker == definition.outputs.end())
        return false;

    std::vector<std::string> remove;
    bool found_marker = false;
    for (const auto& layer : document.layers()) {
        const bool belongs_to_instance =
            std::ranges::any_of(layer.outputs, [&](const auto& output) { return output.instance_id == instance_id; });
        if (!belongs_to_instance)
            continue;

        const bool expected = layer.kind == LayerKind::Marker && layer.pane == LayerPane::Price &&
                              layer.outputs.size() == 1 && layer.outputs.front().output_id == marker->id;
        if (expected && !found_marker)
            found_marker = true;
        else
            remove.push_back(layer.id);
    }
    for (const auto& id : remove)
        document.remove_layer(id);
    if (!found_marker)
        document.add_layer(LayerKind::Marker, {{instance_id, marker->id}},
                           {{1.0F, 0.78F, 0.18F, 1.0F}, true, 2.0F, 0.2F}, LayerPane::Price);

    return !remove.empty() || !found_marker;
}
} // namespace Didrachma::StockChart::Core
