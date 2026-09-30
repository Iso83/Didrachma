#include <Didrachma/analysis/core/condition/Pattern.h>
#include <algorithm>
#include <chrono>

namespace Didrachma::Analysis::Core::Condition::Intern {
std::string parameter_text(const Indicator::ParameterValue& value) {
    return std::visit(
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
}

std::string stable_id(const PatternEventContext& context, const Indicator::Instance& instance,
                      Market::Core::Time::UtcTimestamp timestamp) {
    const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(timestamp.time_since_epoch()).count();
    return context.chart_id + ":" + instance.id + ":" + context.series.provider + ":" + context.series.instrument +
           ":" + std::to_string(context.series.timeframe.quantity) + ":" +
           std::to_string(static_cast<int>(context.series.timeframe.unit)) + ":" + std::to_string(ticks);
}
} // namespace Didrachma::Analysis::Core::Condition::Intern

namespace Didrachma::Analysis::Core::Condition {
std::vector<Event> extract_pattern_events(const Indicator::Definition& definition, const Indicator::Instance& instance,
                                          const Indicator::Result& result,
                                          std::span<const Market::Core::Series::Bar> bars,
                                          const PatternEventContext& context) {
    std::vector<Event> events;
    if (!instance.enabled || definition.capability != Indicator::Capability::AnalysisEvent ||
        result.state != Indicator::CalculationState::Ready)
        return events;

    for (const auto& output : definition.outputs) {
        if (output.visual != Indicator::VisualKind::Marker)
            continue;

        const auto series = std::ranges::find(result.outputs, output.id, &Indicator::OutputSeries::output_id);
        if (series == result.outputs.end())
            continue;

        for (const auto& sample : series->samples) {
            if (sample.value == 0.0)
                continue;

            const auto bar = std::ranges::find(bars, sample.timestamp, &Market::Core::Series::Bar::open_time);
            if (bar == bars.end() || bar->state != Market::Core::Series::BarState::Closed)
                continue;

            auto direction = Direction::Neutral;
            if (output.event_direction == Indicator::EventDirection::Signed)
                direction = sample.value > 0.0 ? Direction::Upward : Direction::Downward;
            Event event{Intern::stable_id(context, instance, sample.timestamp), definition.id, context.series,
                        sample.timestamp};
            event.direction = direction;
            event.severity = Severity::Attention;
            event.title = context.instance_name;
            event.summary = definition.display_name + " returned " + std::to_string(sample.value);
            event.evidence.emplace("raw_pattern_value", sample.value);
            event.evidence.emplace("input_revision", static_cast<double>(result.input_revision));
            event.evidence.emplace("calculation_revision", static_cast<double>(result.revision));
            event.chart_id = context.chart_id;
            event.source_instance_id = instance.id;
            event.source_name = context.instance_name;
            event.source_definition_id = definition.id;
            for (const auto& [name, value] : instance.parameters)
                event.source_parameters.emplace(name, Intern::parameter_text(value));
            events.push_back(std::move(event));
        }
    }

    return events;
}
} // namespace Didrachma::Analysis::Core::Condition
