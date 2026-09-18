#include "Parser.h"

#include <Didrachma/market/core/series/Resampler.h>
#include <Didrachma/market/providers/yahoo/Interval.h>
#include <nlohmann/json.hpp>

using namespace Didrachma::Market::Core::Series;
using namespace Didrachma::Market::Core::Time;

namespace Didrachma::Market::Providers::Yahoo::Intern {
std::chrono::seconds duration(Frame timeframe) {
    using enum Unit;
    switch (timeframe.unit) {
        case Minute:
            return std::chrono::minutes{timeframe.quantity};
        case Hour:
            return std::chrono::hours{timeframe.quantity};
        case Day:
            return std::chrono::days{timeframe.quantity};
    }
    return {};
}

std::vector<Bar> parse_chart(const nlohmann::json& response, Frame timeframe) {
    const auto& chart = response.at("chart");
    if (!chart.at("error").is_null()) {
        const auto& error = chart.at("error");
        const auto code = error.value("code", "Unknown");
        const auto description = error.value("description", "Yahoo Finance returned an error");
        throw ResponseError(code, description);
    }

    const auto& result = chart.at("result");
    if (!result.is_array() || result.empty())
        throw std::runtime_error("Yahoo Finance returned no chart result");

    const auto& series = result.at(0);
    const auto timestamps = series.find("timestamp");
    if (timestamps == series.end() || !timestamps->is_array() || timestamps->empty())
        return {};

    const auto& quote = series.at("indicators").at("quote").at(0);
    const auto& opens = quote.at("open");
    const auto& highs = quote.at("high");
    const auto& lows = quote.at("low");
    const auto& closes = quote.at("close");
    const auto& volumes = quote.at("volume");
    const auto count =
        std::min({timestamps->size(), opens.size(), highs.size(), lows.size(), closes.size(), volumes.size()});

    std::vector<Bar> data;
    data.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if ((*timestamps)[i].is_null() || opens[i].is_null() || highs[i].is_null() || lows[i].is_null() ||
            closes[i].is_null() || volumes[i].is_null())
            continue;

        const auto timestamp = (*timestamps)[i].get<std::int64_t>();
        const auto open_time = UtcTimestamp{std::chrono::seconds{timestamp}};
        data.push_back({open_time, open_time + duration(timeframe), opens[i].get<double>(), highs[i].get<double>(),
                        lows[i].get<double>(), closes[i].get<double>(), volumes[i].get<double>(), BarState::Closed});
    }
    return data;
}

std::vector<Bar> parse_history(const nlohmann::json& response, Frame timeframe) {
    const auto* interval = find_interval(timeframe);
    if (!interval)
        throw std::runtime_error("Yahoo Finance does not support this timeframe");

    auto bars = parse_chart(response, interval->source_frame);
    if (!interval->derived())
        return bars;

    auto result = resample(bars, interval->source_frame, interval->frame);
    if (!result.error.empty())
        throw std::runtime_error(result.error);

    return std::move(result.bars);
}
} // namespace Didrachma::Market::Providers::Yahoo::Intern
