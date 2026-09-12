#pragma once

#include <Didrachma/market/core/series/Bar.h>
#include <Didrachma/market/core/series/Key.h>
#include <variant>

namespace Didrachma::Market::Core::Provider {
struct HistoryRequest {
    Series::Key key;
    Time::Range range;
};

struct Error {
    std::string message;
};

using HistoryResult = std::variant<std::vector<Series::Bar>, Error>;
} // namespace Didrachma::Market::Core::Provider
