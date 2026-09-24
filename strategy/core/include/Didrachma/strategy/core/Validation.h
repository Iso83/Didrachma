#pragma once

#include <Didrachma/analysis/core/indicator/Definition.h>
#include <Didrachma/strategy/core/Model.h>
#include <span>
#include <string>
#include <vector>

namespace Didrachma::Strategy::Core {
enum class ValidationCode {
    MissingValue,
    DuplicateId,
    MissingReference,
    DependencyCycle,
    InvalidIndicatorParameter,
    InvalidTimeframe,
    InvalidInstrument,
    InvalidPricePolicy,
    InvalidCondition,
    InvalidAction,
    InvalidEntryOrder,
    InvalidBacktestRequest
};

struct ValidationError {
    ValidationCode code{};
    std::string path;
    std::string message;
};

using IndicatorCatalog = std::span<const Analysis::Core::Indicator::Definition>;
[[nodiscard]] std::vector<ValidationError> validate(const Definition&, IndicatorCatalog);
} // namespace Didrachma::Strategy::Core
