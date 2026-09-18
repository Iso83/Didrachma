#pragma once

#include <Didrachma/analysis/core/condition/Event.h>
#include <Didrachma/analysis/core/indicator/Definition.h>
#include <Didrachma/market/core/provider/History.h>
#include <Didrachma/stockChart/core/Document.h>
#include <optional>
#include <span>

namespace Didrachma::Studio::Core {
struct EventFilter {
    std::optional<std::string> instrument;
    std::optional<std::string> condition_id;
    std::optional<std::string> chart_id;
};

class EventList {
    std::vector<Analysis::Core::Condition::Event> m_events;

public:
    [[nodiscard]] std::span<const Analysis::Core::Condition::Event> events() const {
        return m_events;
    }

    void publish(std::span<const Analysis::Core::Condition::Event> events);
    void clear_chart(const std::string& chart_id);
    void replace(std::string chart_id, std::string source_instance_id, std::string source_name,
                 std::string source_definition_id, std::map<std::string, std::string> source_parameters,
                 std::span<const Analysis::Core::Condition::Event> events);
    [[nodiscard]] std::vector<const Analysis::Core::Condition::Event*> filtered(const EventFilter& filter) const;
    [[nodiscard]] const Analysis::Core::Condition::Event* find(const std::string& event_id) const;
};

class ConditionBindingRegistry {
    std::map<std::string, bool> m_enabled{{"price-cross", true}, {"band-breakout", true}};

public:
    bool set_enabled(const std::string& binding_id, bool enabled);
    void evaluate(const StockChart::Core::Document& document, std::span<const Market::Core::Series::Bar> bars,
                  EventList& events, std::span<const Analysis::Core::Indicator::Definition> definitions = {}) const;
};

struct EventNavigation {
    Market::Core::Time::Range revealed_range;
    std::optional<Market::Core::Provider::HistoryRequest> missing_history;
};

EventNavigation navigate_to_event(StockChart::Core::Document& document, const Analysis::Core::Condition::Event& event,
                                  std::optional<Market::Core::Time::Range> available_history);
} // namespace Didrachma::Studio::Core
