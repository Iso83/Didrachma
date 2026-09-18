#include <Didrachma/stockChart/core/Document.h>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Didrachma::StockChart::Core {
Document::Document(std::string id, Market::Core::Series::Key series, Market::Core::Time::Range visible_range)
    : m_id(std::move(id)), m_series(std::move(series)), m_visible_range(visible_range) {
    if (m_id.empty() || m_series.instrument.empty() || visible_range.empty())
        throw std::invalid_argument("A chart requires an id, instrument, and non-empty visible range");
}

std::string Document::add_indicator(std::string definition_id,
                                    std::map<std::string, Analysis::Core::Indicator::ParameterValue> parameters) {
    const auto id = m_id + ":indicator:" + std::to_string(m_next_id++);
    const auto name = definition_id;
    m_indicators.push_back({{id, std::move(definition_id), true, std::move(parameters)}, std::nullopt, name});
    return id;
}

bool Document::remove_indicator(const std::string& instance_id) {
    const auto old_size = m_indicators.size();
    std::erase_if(m_indicators, [&](const auto& item) { return item.instance.id == instance_id; });
    std::erase_if(m_layers, [&](const auto& layer) {
        return std::ranges::any_of(layer.outputs,
                                   [&](const auto& binding) { return binding.instance_id == instance_id; });
    });
    reconcile_standalone_indicator_selection();
    return old_size != m_indicators.size();
}

bool Document::set_indicator_enabled(const std::string& instance_id, bool enabled) {
    auto* entry = find_indicator(instance_id);
    if (!entry)
        return false;

    entry->instance.enabled = enabled;
    reconcile_standalone_indicator_selection();
    return true;
}

bool Document::move_indicator(const std::string& instance_id, std::size_t new_index) {
    const auto found =
        std::ranges::find_if(m_indicators, [&](const auto& item) { return item.instance.id == instance_id; });
    if (found == m_indicators.end() || new_index >= m_indicators.size())
        return false;

    auto entry = std::move(*found);
    const auto old_index = static_cast<std::size_t>(std::distance(m_indicators.begin(), found));
    m_indicators.erase(found);
    m_indicators.insert(m_indicators.begin() + static_cast<std::ptrdiff_t>(new_index), std::move(entry));
    return old_index != new_index;
}

bool Document::set_indicator_parameters(const std::string& instance_id,
                                        std::map<std::string, Analysis::Core::Indicator::ParameterValue> parameters) {
    auto* entry = find_indicator(instance_id);
    if (!entry || entry->instance.parameters == parameters)
        return false;

    entry->instance.parameters = std::move(parameters);
    entry->cached_result.reset();
    return true;
}

bool Document::set_indicator_name(const std::string& instance_id, std::string name) {
    auto* entry = find_indicator(instance_id);
    if (!entry || name.empty() || entry->name == name)
        return false;

    entry->name = std::move(name);
    return true;
}

std::vector<std::string> Document::standalone_indicator_ids() const {
    std::vector<std::string> ids;
    for (const auto& entry : m_indicators) {
        if (!entry.instance.enabled)
            continue;

        const auto has_visible_layer = std::ranges::any_of(m_layers, [&](const auto& layer) {
            return layer.pane == LayerPane::Separate && layer.style.visible &&
                   std::ranges::any_of(layer.outputs,
                                       [&](const auto& output) { return output.instance_id == entry.instance.id; });
        });
        if (has_visible_layer)
            ids.push_back(entry.instance.id);
    }
    return ids;
}

bool Document::select_standalone_indicator(const std::string& instance_id) {
    const auto ids = standalone_indicator_ids();
    if (std::ranges::find(ids, instance_id) == ids.end() || m_selected_standalone_indicator_id == instance_id)
        return false;

    m_selected_standalone_indicator_id = instance_id;
    return true;
}

std::string Document::add_layer(LayerKind kind, std::vector<OutputBinding> outputs, VisualStyle style, LayerPane pane) {
    if (outputs.empty() || (kind == LayerKind::Band && outputs.size() != 2))
        throw std::invalid_argument("A layer requires outputs and a band requires exactly two");

    for (const auto& output : outputs)
        if (!find_indicator(output.instance_id))
            throw std::invalid_argument("Layer output references an unknown indicator instance");

    const auto id = m_id + ":layer:" + std::to_string(m_next_id++);
    m_layers.push_back({id, kind, std::move(outputs), style, 1, pane});
    reconcile_standalone_indicator_selection();
    return id;
}

bool Document::set_layer_style(const std::string& layer_id, VisualStyle style) {
    const auto found = std::ranges::find_if(m_layers, [&](const auto& layer) { return layer.id == layer_id; });
    if (found == m_layers.end())
        return false;

    if (found->style != style) {
        found->style = style;
        ++found->style_revision;
        reconcile_standalone_indicator_selection();
    }
    return true;
}

Layer* Document::find_layer(const std::string& layer_id) {
    const auto found = std::ranges::find(m_layers, layer_id, &Layer::id);
    return found == m_layers.end() ? nullptr : &*found;
}

bool Document::remove_layer(const std::string& layer_id) {
    const auto removed = std::erase_if(m_layers, [&](const auto& layer) { return layer.id == layer_id; }) != 0;
    if (removed)
        reconcile_standalone_indicator_selection();

    return removed;
}

void Document::reconcile_standalone_indicator_selection() {
    const auto ids = standalone_indicator_ids();
    if (m_selected_standalone_indicator_id && std::ranges::find(ids, *m_selected_standalone_indicator_id) != ids.end())
        return;

    m_selected_standalone_indicator_id = ids.empty() ? std::nullopt : std::optional{ids.front()};
}

std::vector<NavigationEvent> Document::dispatch(const NavigationCommand& command) {
    return std::visit(
        [&](const auto& value) -> std::vector<NavigationEvent> {
            using Command = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Command, NavigateViewport>) {
                if (value.range.empty())
                    return {};

                m_visible_range = value.range;
                return {{NavigationEventKind::ViewportChanged}};
            } else if constexpr (std::is_same_v<Command, SelectTimestamp>) {
                m_selected_timestamp = value.timestamp;
                return {{NavigationEventKind::TimestampSelected}};
            } else if constexpr (std::is_same_v<Command, SelectRange>) {
                m_selected_range = value.range;
                return {{NavigationEventKind::RangeSelected}};
            } else {
                m_selected_event_id = value.event_id;
                return {{NavigationEventKind::AnalysisEventSelected}};
            }
        },
        command);
}
} // namespace Didrachma::StockChart::Core
