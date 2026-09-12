#include "YahooStockDataAdapter.h"

using namespace Didrachma::Apps::Chart::Data;
using namespace Didrachma::Market::Core::Time;

namespace Didrachma::Apps::Chart::Adapters {

Market::Core::Time::UtcTimestamp parse_date(const std::string& value) {
    std::tm tm{};
    std::istringstream input(value);
    input >> std::get_time(&tm, "%Y-%m-%d");

#ifdef _WIN32
    const std::time_t utc_seconds = _mkgmtime(&tm);
#else
    const std::time_t utc_seconds = timegm(&tm);
#endif

    return Market::Core::Time::UtcTimestamp{std::chrono::seconds{utc_seconds}};
}

Market::Core::Time::Frame timeframe(Interval value) {
    using enum Market::Core::Time::Unit;

    return value == Interval_Daily    ? Market::Core::Time::Frame{1, Day}
           : value == Interval_Weekly ? Market::Core::Time::Frame{7, Day}
                                      : Market::Core::Time::Frame{30, Day};
}

Data::TickerData YahooStockDataAdapter::get_ticker(std::string ticker, std::string start, std::string end,
                                                   Interval interval) {
    auto result = m_provider.load_history(
        {{"yahoo", ticker, timeframe(interval)}, {parse_date(start), parse_date(end) + std::chrono::days{1}}});
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
