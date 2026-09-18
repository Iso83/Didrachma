#include <Didrachma/analysis/core/condition/BandBreakout.h>
#include <algorithm>

namespace Didrachma::Analysis::Core::Condition {
namespace Intern {
const NamedIndicatorInput* input(std::span<const NamedIndicatorInput> inputs, const std::string& name) {
    const auto found = std::ranges::find(inputs, name, &NamedIndicatorInput::name);
    return found == inputs.end() ? nullptr : &*found;
}

const Indicator::OutputSample* at(const NamedIndicatorInput& input, Market::Core::Time::UtcTimestamp timestamp) {
    const auto found = std::ranges::lower_bound(input.samples, timestamp, {}, &Indicator::OutputSample::timestamp);
    return found != input.samples.end() && found->timestamp == timestamp ? &*found : nullptr;
}
} // namespace Intern

std::vector<Event> BandBreakout::evaluate(const Inputs& inputs) const {
    const auto prices = std::ranges::find(inputs.market, "price", &NamedMarketInput::name);
    const auto* upper = Intern::input(inputs.indicators, "upper");
    const auto* lower = Intern::input(inputs.indicators, "lower");
    if (prices == inputs.market.end() || !upper || !lower)
        return {};

    struct Aligned {
        Market::Core::Time::UtcTimestamp timestamp;
        double close, upper, lower;
    };
    std::vector<Aligned> aligned;
    for (const auto& bar : prices->bars) {
        if (bar.state != Market::Core::Series::BarState::Closed)
            continue;

        const auto* upper_at = Intern::at(*upper, bar.open_time);
        const auto* lower_at = Intern::at(*lower, bar.open_time);
        if (!upper_at || !lower_at)
            continue;

        aligned.push_back({bar.open_time, bar.close, upper_at->value, lower_at->value});
    }

    std::vector<Event> events;
    for (std::size_t index = 1; index < aligned.size(); ++index) {
        const auto& previous = aligned[index - 1];
        const auto& current = aligned[index];
        const bool upward = previous.close <= previous.upper && current.close > current.upper;
        const bool downward = previous.close >= previous.lower && current.close < current.lower;
        if (!upward && !downward)
            continue;

        const auto seconds =
            std::chrono::duration_cast<std::chrono::seconds>(current.timestamp.time_since_epoch()).count();
        events.push_back({m_id + ":" + inputs.series.instrument + ":" + std::to_string(seconds),
                          m_id,
                          inputs.series,
                          current.timestamp,
                          std::nullopt,
                          upward ? Direction::Upward : Direction::Downward,
                          Severity::Attention,
                          upward ? "Close above Bollinger band" : "Close below Bollinger band",
                          upward ? "Close finished above the upper band" : "Close finished below the lower band",
                          {{"close", current.close}, {"upper", current.upper}, {"lower", current.lower}}});
    }
    return events;
}
} // namespace Didrachma::Analysis::Core::Condition
