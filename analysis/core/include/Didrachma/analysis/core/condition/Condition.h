#pragma once

#include <Didrachma/analysis/core/condition/Event.h>
#include <Didrachma/analysis/core/indicator/Result.h>
#include <Didrachma/market/core/series/Bar.h>
#include <span>
#include <vector>

namespace Didrachma::Analysis::Core::Condition {
struct NamedMarketInput {
    std::string name;
    std::span<const Market::Core::Series::Bar> bars;
};

struct NamedIndicatorInput {
    std::string name;
    std::span<const Indicator::OutputSample> samples;
};

struct Inputs {
    Market::Core::Series::Key series;
    std::span<const NamedMarketInput> market;
    std::span<const NamedIndicatorInput> indicators;
};

class Definition {
public:
    virtual ~Definition() = default;

    [[nodiscard]] virtual const std::string& id() const = 0;
    [[nodiscard]] virtual std::vector<Event> evaluate(const Inputs& inputs) const = 0;
};
} // namespace Didrachma::Analysis::Core::Condition
