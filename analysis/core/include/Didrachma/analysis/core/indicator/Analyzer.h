#pragma once

#include <Didrachma/analysis/core/indicator/Calculation.h>
#include <Didrachma/analysis/core/indicator/Definition.h>
#include <vector>

namespace Didrachma::Analysis::Core::Indicator {
class Analyzer {
public:
    virtual ~Analyzer() = default;

    [[nodiscard]] virtual std::vector<Definition> catalog() const = 0;
    // The number of source bars needed before the first deterministic output can be produced.
    // Adapters should override this when their native implementation owns the lookback rules.
    [[nodiscard]] virtual std::size_t required_history(const Instance&) const {
        return 1;
    }
    virtual CalculationOutcome calculate(const CalculationRequest&) = 0;
};
} // namespace Didrachma::Analysis::Core::Indicator
