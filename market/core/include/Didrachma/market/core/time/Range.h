#pragma once

#include <chrono>

namespace Didrachma::Market::Core::Time {
using UtcTimestamp = std::chrono::sys_seconds;

struct Range {
    UtcTimestamp begin;
    UtcTimestamp end;
    [[nodiscard]] bool empty() const {
        return begin >= end;
    }
};
} // namespace Didrachma::Market::Core::Time
