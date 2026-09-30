#include "UtcTime.h"

#include <charconv>
#include <chrono>
#include <format>

namespace Didrachma::Apps::Strategy {
namespace {
std::optional<int> number(std::string_view value) {
    int result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() ? std::optional{result} : std::nullopt;
}
} // namespace

std::optional<Market::Core::Time::UtcTimestamp> parse_utc(std::string_view value) {
    if (value.size() != 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' ||
        value[16] != ':' || value[19] != 'Z')
        return {};

    const auto year = number(value.substr(0, 4));
    const auto month = number(value.substr(5, 2));
    const auto day = number(value.substr(8, 2));
    const auto hour = number(value.substr(11, 2));
    const auto minute = number(value.substr(14, 2));
    const auto second = number(value.substr(17, 2));
    if (!year || !month || !day || !hour || !minute || !second)
        return {};

    const std::chrono::year_month_day date{std::chrono::year{*year}, std::chrono::month{static_cast<unsigned>(*month)},
                                           std::chrono::day{static_cast<unsigned>(*day)}};
    if (!date.ok() || *hour < 0 || *hour > 23 || *minute < 0 || *minute > 59 || *second < 0 || *second > 59)
        return {};

    return std::chrono::sys_days{date} + std::chrono::hours{*hour} + std::chrono::minutes{*minute} +
           std::chrono::seconds{*second};
}

std::string format_utc(Market::Core::Time::UtcTimestamp value) {
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", value);
}
} // namespace Didrachma::Apps::Strategy
