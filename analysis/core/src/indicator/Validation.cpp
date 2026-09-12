#include <Didrachma/analysis/core/indicator/Validation.h>

namespace Didrachma::Analysis::Core::Indicator {
bool matches(ParameterKind kind, const ParameterValue& value) {
    switch (kind) {
        case ParameterKind::Integer:
            return std::holds_alternative<std::int64_t>(value);
        case ParameterKind::Real:
            return std::holds_alternative<double>(value);
        case ParameterKind::Boolean:
            return std::holds_alternative<bool>(value);
        case ParameterKind::Text:
            return std::holds_alternative<std::string>(value);
    }
    return false;
}

std::optional<double> numeric(const ParameterValue& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value))
        return static_cast<double>(*integer);

    if (const auto* real = std::get_if<double>(&value))
        return *real;

    return std::nullopt;
}

std::optional<ValidationError> validate_parameters(const Definition& definition,
                                                   const std::map<std::string, ParameterValue>& parameters) {
    for (const auto& parameter : definition.parameters) {
        const auto value = parameters.find(parameter.id);
        if (value == parameters.end())
            return ValidationError{"Missing parameter: " + parameter.id};

        if (!matches(parameter.kind, value->second))
            return ValidationError{"Invalid type for parameter: " + parameter.id};

        const auto number = numeric(value->second);
        if (number && parameter.minimum && *number < *parameter.minimum)
            return ValidationError{"Parameter below minimum: " + parameter.id};

        if (number && parameter.maximum && *number > *parameter.maximum)
            return ValidationError{"Parameter above maximum: " + parameter.id};
    }

    return std::nullopt;
}
} // namespace Didrachma::Analysis::Core::Indicator
