#pragma once

#include <Didrachma/analysis/core/indicator/Parameter.h>
#include <vector>

namespace Didrachma::Analysis::Core::Indicator {
enum class VisualKind { Line, Band, Histogram, Marker, TimeSpan };

struct OutputDefinition {
    std::string id;
    std::string display_name;
    VisualKind visual{VisualKind::Line};
};

struct Definition {
    std::string id;
    std::string display_name;
    std::vector<ParameterDefinition> parameters;
    std::vector<OutputDefinition> outputs;
};
} // namespace Didrachma::Analysis::Core::Indicator
