#include <Didrachma/analysis/core/condition/PriceCross.h>
#include <algorithm>

namespace Didrachma::Analysis::Core::Condition {
namespace Intern {
const NamedMarketInput* find_market(std::span<const NamedMarketInput> inputs, const std::string& name) {
    const auto found = std::ranges::find(inputs, name, &NamedMarketInput::name);
    return found == inputs.end() ? nullptr : &*found;
}

const NamedIndicatorInput* find_indicator(std::span<const NamedIndicatorInput> inputs, const std::string& name) {
    const auto found = std::ranges::find(inputs, name, &NamedIndicatorInput::name);
    return found == inputs.end() ? nullptr : &*found;
}
} // namespace Intern

std::vector<Event> PriceCross::evaluate(const Inputs& inputs) const {
    const auto* prices = Intern::find_market(inputs.market, "price");
    const auto* average = Intern::find_indicator(inputs.indicators, "ma50");
    if (!prices || !average)
        return {};

    std::vector<std::pair<Market::Core::Time::UtcTimestamp, std::pair<double, double>>> aligned;
    for (const auto& bar : prices->bars) {
        if (bar.state != Market::Core::Series::BarState::Closed)
            continue;

        const auto sample =
            std::ranges::lower_bound(average->samples, bar.open_time, {}, &Indicator::OutputSample::timestamp);
        if (sample != average->samples.end() && sample->timestamp == bar.open_time)
            aligned.push_back({bar.open_time, {bar.close, sample->value}});
    }

    std::vector<Event> events;
    for (std::size_t index = 1; index < aligned.size(); ++index) {
        const auto [previous_price, previous_average] = aligned[index - 1].second;
        const auto [price, moving_average] = aligned[index].second;
        const bool upward = previous_price <= previous_average && price > moving_average;
        const bool downward = previous_price >= previous_average && price < moving_average;
        if (!upward && !downward)
            continue;

        const auto timestamp = aligned[index].first;
        const auto direction = upward ? Direction::Upward : Direction::Downward;
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timestamp.time_since_epoch()).count();
        const auto word = upward ? "above" : "below";
        events.push_back(
            {m_id + ":" + inputs.series.instrument + ":" + std::to_string(seconds) + (upward ? ":up" : ":down"),
             m_id,
             inputs.series,
             timestamp,
             std::nullopt,
             direction,
             Severity::Attention,
             "Price crossed moving average",
             std::string{"Close crossed "} + word + " the configured moving average",
             {{"close", price},
              {"ma50", moving_average},
              {"previousClose", previous_price},
              {"previousMa50", previous_average}}});
    }

    return events;
}
} // namespace Didrachma::Analysis::Core::Condition
