#pragma once

#include <Didrachma/analysis/core/indicator/Calculation.h>
#include <Didrachma/analysis/core/indicator/Definition.h>

namespace Didrachma::Analysis::Core::Indicator {
class Analyzer {
public:
    virtual ~Analyzer() = default;

    [[nodiscard]] virtual std::vector<Definition> catalog() const = 0;
    virtual CalculationOutcome calculate(const CalculationRequest&) = 0;
};
} // namespace Didrachma::Analysis::Core::Indicator
