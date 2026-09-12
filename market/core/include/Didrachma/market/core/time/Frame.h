#pragma once

#include <cstdint>

namespace Didrachma::Market::Core::Time {
enum class Unit { Minute, Hour, Day };

struct Frame {
    std::uint32_t quantity{1};
    Unit unit{Unit::Day};
    friend bool operator==(const Frame&, const Frame&) = default;
};
} // namespace Didrachma::Market::Core::Time
