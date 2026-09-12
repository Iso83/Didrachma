#include "Parser.h"

#include <nlohmann/json.hpp>

using namespace Didrachma::Market::Core::Series;
using namespace Didrachma::Market::Core::Time;

namespace Didrachma::Market::Providers::Yahoo::Intern {
std::vector<Bar> parse_chart(const nlohmann::json& response) {
    const auto& chart = response.at("chart");
    if (!chart.at("error").is_null())
        throw std::runtime_error(chart.at("error").value("description", "Yahoo Finance returned an error"));

    const auto& result = chart.at("result");
    if (!result.is_array() || result.empty())
        throw std::runtime_error("Yahoo Finance returned no chart result");

    const auto& series = result.at(0);
    const auto& timestamps = series.at("timestamp");
    const auto& quote = series.at("indicators").at("quote").at(0);
    const auto& opens = quote.at("open");
    const auto& highs = quote.at("high");
    const auto& lows = quote.at("low");
    const auto& closes = quote.at("close");
    const auto& volumes = quote.at("volume");
    const auto count =
        std::min({timestamps.size(), opens.size(), highs.size(), lows.size(), closes.size(), volumes.size()});

    std::vector<Bar> data;
    data.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (timestamps[i].is_null() || opens[i].is_null() || highs[i].is_null() || lows[i].is_null() ||
            closes[i].is_null() || volumes[i].is_null())
            continue;

        const auto timestamp = timestamps[i].get<std::int64_t>();
        constexpr std::int64_t seconds_per_day = 24 * 60 * 60;
        const auto day = UtcTimestamp{std::chrono::seconds{timestamp - timestamp % seconds_per_day}};
        data.push_back({day, day + std::chrono::days{1}, opens[i].get<double>(), highs[i].get<double>(),
                        lows[i].get<double>(), closes[i].get<double>(), volumes[i].get<double>(), BarState::Closed});
    }
    if (data.empty())
        throw std::runtime_error("Yahoo Finance returned no usable price rows");

    return data;
}
} // namespace Didrachma::Market::Providers::Yahoo::Intern
