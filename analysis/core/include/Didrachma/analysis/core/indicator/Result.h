#pragma once

#include <Didrachma/market/core/time/Range.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Didrachma::Analysis::Core::Indicator {
enum class CalculationState { Ready, InsufficientHistory, Error };

enum class CalculationErrorCode { UnknownDefinition, InvalidParameters, MissingInput, LibraryFailure };

struct CalculationError {
    CalculationErrorCode code{};
    std::string message;
};

struct OutputSample {
    Market::Core::Time::UtcTimestamp timestamp;
    double value{};
};

struct OutputSeries {
    std::string output_id;
    std::vector<OutputSample> samples;
};

struct Result {
    std::vector<OutputSeries> outputs;
    std::uint64_t input_revision{};
    std::uint64_t revision{};
    CalculationState state{CalculationState::InsufficientHistory};
    std::optional<CalculationError> error;
    std::size_t required_history{};
};
} // namespace Didrachma::Analysis::Core::Indicator
