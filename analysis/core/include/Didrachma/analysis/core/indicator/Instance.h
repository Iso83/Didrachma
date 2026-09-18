#pragma once

#include <Didrachma/analysis/core/indicator/Parameter.h>
#include <map>
#include <string>

namespace Didrachma::Analysis::Core::Indicator {
struct Instance {
    std::string id;
    std::string definition_id;
    bool enabled{true};
    std::map<std::string, ParameterValue> parameters;
};
} // namespace Didrachma::Analysis::Core::Indicator
