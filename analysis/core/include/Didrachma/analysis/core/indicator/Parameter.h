#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace Didrachma::Analysis::Core::Indicator {
using ParameterValue = std::variant<std::int64_t, double, bool, std::string>;

enum class ParameterKind { Integer, Real, Boolean, Text };

struct ParameterDefinition {
    std::string id;
    std::string display_name;
    ParameterKind kind{};
    ParameterValue default_value;
    std::optional<double> minimum;
    std::optional<double> maximum;
};
} // namespace Didrachma::Analysis::Core::Indicator
