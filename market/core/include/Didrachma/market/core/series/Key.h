#pragma once

#include <Didrachma/market/core/time/Frame.h>
#include <string>

namespace Didrachma::Market::Core::Series {
struct Key {
    std::string provider;
    std::string instrument;
    Time::Frame timeframe;
    friend bool operator==(const Key&, const Key&) = default;
};
} // namespace Didrachma::Market::Core::Series
