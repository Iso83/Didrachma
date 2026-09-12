#include "TestAssert.h"

#include <Didrachma/analysis/core/indicator/Validation.h>

using namespace Didrachma::Analysis::Core::Indicator;

Definition definition() {
    return {"sma",
            "Simple Moving Average",
            {{"period", "Period", ParameterKind::Integer, std::int64_t{20}, 2, 200}},
            {{"value", "SMA", VisualKind::Line}}};
}

int test_parameter_validation() {
    CPPTEST_ASSERT(!validate_parameters(definition(), {{"period", std::int64_t{20}}}));
    CPPTEST_ASSERT(validate_parameters(definition(), {{"period", 20.0}}).has_value());
    CPPTEST_ASSERT(validate_parameters(definition(), {{"period", std::int64_t{1}}}).has_value());
    CPPTEST_ASSERT(validate_parameters(definition(), {}).has_value());
    return 0;
}

int main() {
    CPPTEST_RUN(test_parameter_validation);
    return 0;
}
