#pragma once

#include <Didrachma/analysis/core/indicator/Instance.h>
#include <Didrachma/analysis/core/indicator/Result.h>
#include <Didrachma/market/core/series/Key.h>
#include <Didrachma/stockChart/core/Layer.h>
#include <Didrachma/stockChart/core/Navigation.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>

namespace Didrachma::StockChart::Core {
enum class PriceRangeMode { Automatic, Manual };

struct IndicatorEntry {
    Analysis::Core::Indicator::Instance instance;
    std::optional<Analysis::Core::Indicator::Result> cached_result;
    std::string name;
};

class Document {
    std::string m_id;
    Market::Core::Series::Key m_series;
    Market::Core::Time::Range m_visible_range;
    PriceRangeMode m_price_range_mode{PriceRangeMode::Automatic};
    bool m_auto_follow{true};
    float m_price_pane_ratio{0.76F};
    float m_separate_pane_ratio{0.72F};
    bool m_show_hover_values{true};
    std::vector<IndicatorEntry> m_indicators;
    std::vector<Layer> m_layers;
    std::optional<std::string> m_selected_standalone_indicator_id;
    std::optional<Market::Core::Time::UtcTimestamp> m_selected_timestamp;
    std::optional<Market::Core::Time::Range> m_selected_range;
    std::optional<std::string> m_selected_event_id;
    std::uint64_t m_next_id{1};

public:
    Document(std::string id, Market::Core::Series::Key series, Market::Core::Time::Range visible_range);

    [[nodiscard]] const std::string& id() const {
        return m_id;
    }
    [[nodiscard]] const Market::Core::Series::Key& series() const {
        return m_series;
    }
    [[nodiscard]] Market::Core::Time::Range visible_range() const {
        return m_visible_range;
    }
    [[nodiscard]] PriceRangeMode price_range_mode() const {
        return m_price_range_mode;
    }
    [[nodiscard]] bool auto_follow() const {
        return m_auto_follow;
    }
    [[nodiscard]] float price_pane_ratio() const {
        return m_price_pane_ratio;
    }
    [[nodiscard]] float separate_pane_ratio() const {
        return m_separate_pane_ratio;
    }
    [[nodiscard]] bool show_hover_values() const {
        return m_show_hover_values;
    }
    [[nodiscard]] std::span<const IndicatorEntry> indicators() const {
        return m_indicators;
    }
    [[nodiscard]] std::span<const Layer> layers() const {
        return m_layers;
    }
    [[nodiscard]] const std::optional<std::string>& selected_standalone_indicator_id() const {
        return m_selected_standalone_indicator_id;
    }
    [[nodiscard]] const std::optional<Market::Core::Time::UtcTimestamp>& selected_timestamp() const {
        return m_selected_timestamp;
    }
    [[nodiscard]] const std::optional<Market::Core::Time::Range>& selected_range() const {
        return m_selected_range;
    }
    [[nodiscard]] const std::optional<std::string>& selected_event_id() const {
        return m_selected_event_id;
    }

    void set_price_range_mode(PriceRangeMode mode) {
        m_price_range_mode = mode;
    }
    void set_auto_follow(bool enabled) {
        m_auto_follow = enabled;
    }
    void set_price_pane_ratio(float ratio) {
        m_price_pane_ratio = std::clamp(ratio, 0.2F, 0.8F);
    }
    void set_separate_pane_ratio(float ratio) {
        m_separate_pane_ratio = std::clamp(ratio, 0.2F, 0.8F);
    }
    void set_show_hover_values(bool enabled) {
        m_show_hover_values = enabled;
    }

    std::string add_indicator(std::string definition_id,
                              std::map<std::string, Analysis::Core::Indicator::ParameterValue> parameters);
    bool remove_indicator(const std::string& instance_id);
    bool set_indicator_enabled(const std::string& instance_id, bool enabled);
    bool move_indicator(const std::string& instance_id, std::size_t new_index);
    bool set_indicator_parameters(const std::string& instance_id,
                                  std::map<std::string, Analysis::Core::Indicator::ParameterValue> parameters);
    bool set_indicator_name(const std::string& instance_id, std::string name);
    [[nodiscard]] std::vector<std::string> standalone_indicator_ids() const;
    bool select_standalone_indicator(const std::string& instance_id);
    IndicatorEntry* find_indicator(const std::string& instance_id) {
        const auto found =
            std::ranges::find_if(m_indicators, [&](const auto& item) { return item.instance.id == instance_id; });

        return found == m_indicators.end() ? nullptr : &*found;
    }

    std::string add_layer(LayerKind kind, std::vector<OutputBinding> outputs, VisualStyle style = {},
                          LayerPane pane = LayerPane::Price);
    bool set_layer_style(const std::string& layer_id, VisualStyle style);
    [[nodiscard]] Layer* find_layer(const std::string& layer_id);
    bool remove_layer(const std::string& layer_id);
    std::vector<NavigationEvent> dispatch(const NavigationCommand& command);

private:
    void reconcile_standalone_indicator_selection();
};
} // namespace Didrachma::StockChart::Core
