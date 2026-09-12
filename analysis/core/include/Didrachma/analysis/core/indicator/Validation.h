#pragma once

#include <Didrachma/analysis/core/indicator/Definition.h>
#include <map>
#include <optional>

namespace Didrachma::Analysis::Core::Indicator {
struct ValidationError {
    std::string message;
};

std::optional<ValidationError> validate_parameters(const Definition&, const std::map<std::string, ParameterValue>&);
} // namespace Didrachma::Analysis::Core::Indicator
