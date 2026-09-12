#pragma once

#include <Didrachma/analysis/core/indicator/Instance.h>
#include <Didrachma/analysis/core/indicator/Result.h>
#include <Didrachma/market/core/series/Bar.h>
#include <span>

namespace Didrachma::Analysis::Core::Indicator {
enum class RecalculationKind { None, Tail, Full };

struct CalculationRequest {
    Instance instance;
    std::span<const Market::Core::Series::Bar> bars;
    std::uint64_t input_revision{};
    std::optional<Market::Core::Time::Range> dirty_range;
};

struct CalculationOutcome {
    Result result;
    RecalculationKind recalculation{RecalculationKind::None};
};

} // namespace Didrachma::Analysis::Core::Indicator
