#pragma once

#include <Didrachma/market/core/series/Bar.h>
#include <Didrachma/market/core/time/Frame.h>
#include <span>
#include <string>
#include <vector>

namespace Didrachma::Market::Core::Series {
enum class SessionPolicy { ContinuousUtc };

struct ResampleResult {
    std::vector<Bar> bars;
    std::string error;
};

[[nodiscard]] ResampleResult resample(std::span<const Bar> input, Time::Frame source, Time::Frame target,
                                      SessionPolicy policy = SessionPolicy::ContinuousUtc);
} // namespace Didrachma::Market::Core::Series
