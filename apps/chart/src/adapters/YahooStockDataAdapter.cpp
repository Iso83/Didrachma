#include "YahooStockDataAdapter.h"

#include <cstdio>
#include <stdexcept>

using namespace Didrachma::Apps::Chart::Data;
using namespace Didrachma::Market::Core::Time;

namespace Didrachma::Apps::Chart::Adapters {

Market::Core::Time::UtcTimestamp parse_date(const std::string& value) {
    int year{}, month{}, day{}, consumed{};
    if (std::sscanf(value.c_str(), "%4d-%2d-%2d%n", &year, &month, &day, &consumed) != 3 ||
        consumed != static_cast<int>(value.size()))
        throw std::invalid_argument("Date must use YYYY-MM-DD format");

    const std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
                                           std::chrono::day{static_cast<unsigned>(day)}};
    if (!date.ok())
        throw std::invalid_argument("Date is not a valid calendar date");

    return std::chrono::sys_days{date};
}

Market::Core::Time::Frame timeframe(Interval value) {
    using enum Market::Core::Time::Unit;

    return value == Interval_Daily    ? Market::Core::Time::Frame{1, Day}
           : value == Interval_Weekly ? Market::Core::Time::Frame{7, Day}
                                      : Market::Core::Time::Frame{30, Day};
}

Data::TickerData YahooStockDataAdapter::get_ticker(std::string ticker, std::string start, std::string end,
                                                   Interval interval) {
    Market::Core::Provider::HistoryResult result;
    try {
        result = m_provider.load_history(
            {{"yahoo", ticker, timeframe(interval)}, {parse_date(start), parse_date(end) + std::chrono::days{1}}});
    } catch (const std::invalid_argument& error) {
        m_error = error.what();
        return TickerData{"ERROR"};
    }
    if (const auto* error = std::get_if<Market::Core::Provider::Error>(&result)) {
        m_error = error->message;
        return TickerData{"ERROR"};
    }

    TickerData output{std::move(ticker)};
    const auto& bars = std::get<std::vector<Market::Core::Series::Bar>>(result);
    output.reserve(static_cast<int>(bars.size()));
    for (const auto& bar : bars)
        output.push_back(static_cast<double>(bar.open_time.time_since_epoch().count()), bar.open, bar.high, bar.low,
                         bar.close, bar.volume);
    m_error.clear();
    return output;
}
} // namespace Didrachma::Apps::Chart::Adapters
