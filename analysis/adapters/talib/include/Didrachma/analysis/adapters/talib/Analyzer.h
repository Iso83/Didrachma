#pragma once

#include <Didrachma/analysis/core/indicator/Analyzer.h>
#include <memory>

namespace Didrachma::Analysis::Adapters::TaLib {
class Analyzer final : public Core::Indicator::Analyzer {
    class Impl;
    std::unique_ptr<Impl> m_impl;

public:
    Analyzer();
    ~Analyzer();
    Analyzer(const Analyzer&) = delete;
    Analyzer(Analyzer&&) noexcept;

    Analyzer& operator=(const Analyzer&) = delete;
    Analyzer& operator=(Analyzer&&) noexcept;

    [[nodiscard]] std::vector<Core::Indicator::Definition> catalog() const override;
    Core::Indicator::CalculationOutcome calculate(const Core::Indicator::CalculationRequest& request) override;
};
} // namespace Didrachma::Analysis::Adapters::TaLib
