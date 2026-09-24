#pragma once

#include <Didrachma/analysis/core/indicator/Instance.h>
#include <Didrachma/analysis/core/indicator/Result.h>
#include <Didrachma/market/core/series/Bar.h>
#include <cstdint>
#include <optional>
#include <span>

namespace Didrachma::Analysis::Core::Indicator {
enum class RecalculationKind { None, Tail, Full };

struct CalculationRequest {
    Instance instance;
    std::span<const Market::Core::Series::Bar> bars;
    std::uint64_t input_revision{};
    std::optional<Market::Core::Time::Range> dirty_range;
    // Dependency planners may expand the range by required_history before dispatch.
    bool dirty_range_includes_lookback{};
};

struct CalculationOutcome {
    Result result;
    RecalculationKind recalculation{RecalculationKind::None};
    std::size_t calculated_input_begin{};
    std::size_t calculated_input_count{};
    std::size_t reused_prefix_samples{};
};

} // namespace Didrachma::Analysis::Core::Indicator
