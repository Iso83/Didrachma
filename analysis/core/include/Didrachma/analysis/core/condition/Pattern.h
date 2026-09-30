#pragma once

#include <Didrachma/analysis/core/condition/Event.h>
#include <Didrachma/analysis/core/indicator/Definition.h>
#include <Didrachma/analysis/core/indicator/Instance.h>
#include <Didrachma/analysis/core/indicator/Result.h>
#include <Didrachma/market/core/series/Bar.h>
#include <span>

namespace Didrachma::Analysis::Core::Condition {
struct PatternEventContext {
    std::string chart_id;
    std::string instance_name;
    Market::Core::Series::Key series;
};

[[nodiscard]] std::vector<Event> extract_pattern_events(const Indicator::Definition& definition,
                                                        const Indicator::Instance& instance,
                                                        const Indicator::Result& result,
                                                        std::span<const Market::Core::Series::Bar> bars,
                                                        const PatternEventContext& context);
} // namespace Didrachma::Analysis::Core::Condition
