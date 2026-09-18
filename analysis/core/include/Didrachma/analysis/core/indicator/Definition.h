#pragma once

#include <Didrachma/analysis/core/indicator/Parameter.h>
#include <optional>
#include <string>
#include <vector>

namespace Didrachma::Analysis::Core::Indicator {
enum class VisualKind { Line, Band, Histogram, Marker, TimeSpan };
enum class MarketInput { Open, High, Low, Close, Volume };
enum class PaneHint { PriceOverlay, Volume, Separate };
enum class RangeHint { Derived, Fixed };
enum class OutputRole { Value, PriceReference, UpperBand, MiddleBand, LowerBand };
enum class Capability { ContinuousStudy, AnalysisEvent };

struct OutputDefinition {
    std::string id;
    std::string display_name;
    VisualKind visual{VisualKind::Line};
    OutputRole role{OutputRole::Value};
};

struct Definition {
    std::string id;
    std::string display_name;
    std::vector<ParameterDefinition> parameters;
    std::vector<OutputDefinition> outputs;
    std::string group;
    std::vector<MarketInput> inputs;
    PaneHint pane{PaneHint::PriceOverlay};
    RangeHint range{RangeHint::Derived};
    std::optional<double> range_min;
    std::optional<double> range_max;
    Capability capability{Capability::ContinuousStudy};
};
} // namespace Didrachma::Analysis::Core::Indicator
