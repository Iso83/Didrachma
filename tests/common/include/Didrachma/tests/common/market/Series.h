#pragma once

#include <Didrachma/market/core/series/Bars.h>

namespace Didrachma::Testing {
using namespace Didrachma::Market::Core::Series;
using namespace Didrachma::Market::Core::Time;

UtcTimestamp at(int seconds) {
    return UtcTimestamp{std::chrono::seconds{seconds}};
}

Bar bar(int seconds, BarState state = BarState::Closed, double close = 1) {
    return {at(seconds), at(seconds + 10), 1, 2, 0, close, 10, state};
}

Key key() {
    return {"fake", "TEST", {10, Unit::Minute}};
}
} // namespace Didrachma::Testing
