#pragma once

#include <Didrachma/market/core/time/Range.h>
#include <optional>
#include <string>
#include <string_view>

namespace Didrachma::Apps::Strategy {
[[nodiscard]] std::optional<Market::Core::Time::UtcTimestamp> parse_utc(std::string_view value);
[[nodiscard]] std::string format_utc(Market::Core::Time::UtcTimestamp value);
} // namespace Didrachma::Apps::Strategy
