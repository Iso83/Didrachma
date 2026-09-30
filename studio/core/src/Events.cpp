#include <Didrachma/analysis/core/condition/BandBreakout.h>
#include <Didrachma/analysis/core/condition/Pattern.h>
#include <Didrachma/analysis/core/condition/PriceCross.h>
#include <Didrachma/studio/core/Events.h>
#include <algorithm>

using Event = Didrachma::Analysis::Core::Condition::Event;
using namespace Didrachma::StockChart::Core;
namespace Provider = Didrachma::Market::Core::Provider;
namespace Time = Didrachma::Market::Core::Time;

namespace Didrachma::Studio::Core {
void EventList::publish(std::span<const Event> events) {
    for (const auto& event : events) {
        const auto found = std::ranges::find(m_events, event.id, &Event::id);
        if (found == m_events.end())
            m_events.push_back(event);
        else
            *found = event;
    }
    std::ranges::sort(m_events, [](const auto& left, const auto& right) {
        return left.start > right.start || (left.start == right.start && left.id < right.id);
    });
}

void EventList::clear_chart(const std::string& chart_id) {
    std::erase_if(m_events, [&](const auto& event) { return event.chart_id == chart_id; });
}

void EventList::replace(std::string chart_id, std::string source_instance_id, std::string source_name,
                        std::string source_definition_id, std::map<std::string, std::string> source_parameters,
                        std::span<const Event> events) {
    std::erase_if(m_events, [&](const auto& event) {
        return event.chart_id == chart_id && event.source_instance_id == source_instance_id;
    });
    for (auto event : events) {
        if (event.chart_id.empty())
            event.id = chart_id + ":" + source_instance_id + ":" + event.id;
        event.chart_id = chart_id;
        event.source_instance_id = source_instance_id;
        event.source_name = source_name;
        event.source_definition_id = source_definition_id;
        event.source_parameters = source_parameters;
        m_events.push_back(std::move(event));
    }
    std::ranges::sort(m_events, [](const auto& left, const auto& right) {
        return left.start > right.start || (left.start == right.start && left.id < right.id);
    });
}

namespace Intern {
const std::vector<Analysis::Core::Indicator::OutputSample>* output(const StockChart::Core::IndicatorEntry& entry,
                                                                   const std::string& id) {
    if (!entry.cached_result || entry.cached_result->state != Analysis::Core::Indicator::CalculationState::Ready)
        return nullptr;
    const auto found =
        std::ranges::find(entry.cached_result->outputs, id, &Analysis::Core::Indicator::OutputSeries::output_id);
    return found == entry.cached_result->outputs.end() ? nullptr : &found->samples;
}

std::map<std::string, std::string> parameters(const Analysis::Core::Indicator::Instance& instance) {
    std::map<std::string, std::string> result;
    for (const auto& [name, value] : instance.parameters)
        result[name] = std::visit(
            [](const auto& item) {
                using Value = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<Value, std::string>)
                    return item;
                else if constexpr (std::is_same_v<Value, bool>)
                    return std::string{item ? "true" : "false"};
                else
                    return std::to_string(item);
            },
            value);
    return result;
}
} // namespace Intern

bool ConditionBindingRegistry::set_enabled(const std::string& binding_id, bool enabled) {
    const auto found = m_enabled.find(binding_id);
    if (found == m_enabled.end())
        return false;
    found->second = enabled;
    return true;
}

void ConditionBindingRegistry::evaluate(const StockChart::Core::Document& document,
                                        std::span<const Market::Core::Series::Bar> bars, EventList& events,
                                        std::span<const Analysis::Core::Indicator::Definition> definitions) const {
    events.clear_chart(document.id());
    const std::vector<Analysis::Core::Condition::NamedMarketInput> market{{"price", bars}};
    for (const auto& entry : document.indicators()) {
        std::vector<Event> found;
        if (entry.instance.enabled && entry.cached_result &&
            entry.cached_result->state == Analysis::Core::Indicator::CalculationState::Ready) {
            const auto definition = std::ranges::find(definitions, entry.instance.definition_id,
                                                      &Analysis::Core::Indicator::Definition::id);
            if (definition != definitions.end() &&
                definition->capability == Analysis::Core::Indicator::Capability::AnalysisEvent)
                found = Analysis::Core::Condition::extract_pattern_events(
                    *definition, entry.instance, *entry.cached_result, bars,
                    {document.id(), entry.name, document.series()});
            if (definition != definitions.end() && found.empty() &&
                definition->capability != Analysis::Core::Indicator::Capability::AnalysisEvent &&
                m_enabled.at("price-cross")) {
                const auto reference =
                    std::ranges::find(definition->outputs, Analysis::Core::Indicator::OutputRole::PriceReference,
                                      &Analysis::Core::Indicator::OutputDefinition::role);
                if (reference != definition->outputs.end())
                    if (const auto* average = Intern::output(entry, reference->id)) {
                        const std::vector<Analysis::Core::Condition::NamedIndicatorInput> inputs{{"ma50", *average}};
                        found = Analysis::Core::Condition::PriceCross{}.evaluate({document.series(), market, inputs});
                    }
            }
            if (definition != definitions.end() && found.empty() && m_enabled.at("band-breakout")) {
                const auto upper_definition =
                    std::ranges::find(definition->outputs, Analysis::Core::Indicator::OutputRole::UpperBand,
                                      &Analysis::Core::Indicator::OutputDefinition::role);
                const auto lower_definition =
                    std::ranges::find(definition->outputs, Analysis::Core::Indicator::OutputRole::LowerBand,
                                      &Analysis::Core::Indicator::OutputDefinition::role);
                const auto* upper = upper_definition == definition->outputs.end()
                                        ? nullptr
                                        : Intern::output(entry, upper_definition->id);
                const auto* lower = lower_definition == definition->outputs.end()
                                        ? nullptr
                                        : Intern::output(entry, lower_definition->id);
                if (upper && lower) {
                    const std::vector<Analysis::Core::Condition::NamedIndicatorInput> inputs{{"upper", *upper},
                                                                                             {"lower", *lower}};
                    found = Analysis::Core::Condition::BandBreakout{}.evaluate({document.series(), market, inputs});
                }
            }
        }
        events.replace(document.id(), entry.instance.id, entry.name, entry.instance.definition_id,
                       Intern::parameters(entry.instance), found);
    }
}

std::vector<const Event*> EventList::filtered(const EventFilter& filter) const {
    std::vector<const Event*> result;
    for (const auto& event : m_events)
        if ((!filter.instrument || event.series.instrument == *filter.instrument) &&
            (!filter.condition_id || event.condition_id == *filter.condition_id) &&
            (!filter.chart_id || event.chart_id == *filter.chart_id))
            result.push_back(&event);

    return result;
}

const Event* EventList::find(const std::string& event_id) const {
    const auto found = std::ranges::find(m_events, event_id, &Event::id);
    return found == m_events.end() ? nullptr : &*found;
}

EventNavigation navigate_to_event(Document& document, const Event& event,
                                  std::optional<Time::Range> available_history) {
    auto duration = document.visible_range().end - document.visible_range().begin;
    if (duration <= Time::UtcTimestamp::duration::zero())
        duration = std::chrono::hours{24};
    const auto event_end = event.end.value_or(event.start);
    const auto event_duration = event_end - event.start;
    if (event_duration >= duration)
        duration = event_duration + event_duration / 5;

    const auto center = event.start + (event_end - event.start) / 2;
    const Market::Core::Time::Range revealed{center - duration / 2, center + duration / 2};
    document.dispatch(NavigateViewport{revealed});
    document.dispatch(SelectAnalysisEvent{event.id});
    if (event.end)
        document.dispatch(SelectRange{Time::Range{event.start, *event.end}});
    else
        document.dispatch(SelectTimestamp{event.start});

    std::optional<Provider::HistoryRequest> request;
    if (!available_history || revealed.begin < available_history->begin || revealed.end > available_history->end)
        request = Provider::HistoryRequest{document.series(), revealed};

    return {revealed, request};
}
} // namespace Didrachma::Studio::Core
