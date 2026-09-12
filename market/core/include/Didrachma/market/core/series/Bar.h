#pragma once

#include <Didrachma/market/core/time/Range.h>
#include <optional>

namespace Didrachma::Market::Core::Series {
enum class BarState { Forming, Closed };

struct Bar {
    Time::UtcTimestamp open_time;
    std::optional<Time::UtcTimestamp> close_time;
    double open{}, high{}, low{}, close{}, volume{};
    BarState state{BarState::Closed};
};
} // namespace Didrachma::Market::Core::Series
