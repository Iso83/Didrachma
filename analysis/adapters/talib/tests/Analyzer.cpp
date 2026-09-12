#include "TestAssert.h"

#include <Didrachma/analysis/adapters/talib/Analyzer.h>
#include <cmath>

using namespace Didrachma::Analysis::Core::Indicator;
using namespace Didrachma::Analysis::Adapters;
using namespace Didrachma::Market::Core::Time;
using namespace Didrachma::Market::Core::Series;

namespace TaLib = Didrachma::Analysis::Adapters::TaLib;

UtcTimestamp at(int day) {
    return UtcTimestamp{std::chrono::hours{24 * day}};
}

std::vector<::Bar> bars(std::initializer_list<double> closes) {
    std::vector<Bar> result;
    int day = 0;
    for (const double close : closes) {
        result.push_back({at(day), at(day + 1), close, close, close, close, 1, BarState::Closed});
        day += day == 2 ? 2 : 1;
    }

    return result;
}

Instance instance(std::string definition, ParameterValue deviation = 2.0) {
    return {"one", std::move(definition), true, {{"period", std::int64_t{3}}, {"deviation", deviation}}};
}

bool near(double left, double right) {
    return std::abs(left - right) < 1e-9;
}

int test_catalog_metadata_and_visuals() {
    TaLib::Analyzer analyzer;
    const auto catalog = analyzer.catalog();
    CPPTEST_ASSERT(catalog.size() == 2);
    CPPTEST_ASSERT(catalog[0].id == "sma");
    CPPTEST_ASSERT(catalog[0].outputs[0].visual == VisualKind::Line);
    CPPTEST_ASSERT(catalog[1].id == "bbands");
    CPPTEST_ASSERT(catalog[1].outputs[0].visual == VisualKind::Band);
    CPPTEST_ASSERT(catalog[1].outputs[1].visual == VisualKind::Line);
    return 0;
}

int test_sma_alignment_gaps_and_state() {
    TaLib::Analyzer analyzer;
    Analyzer& neutral_analyzer = analyzer;
    auto input = bars({1, 2, 3, 4, 5});
    auto configured = instance("sma");

    const auto calculated = neutral_analyzer.calculate({configured, input, 1, std::nullopt});
    CPPTEST_ASSERT(calculated.recalculation == RecalculationKind::Full);
    CPPTEST_ASSERT(calculated.result.state == CalculationState::Ready);
    CPPTEST_ASSERT(calculated.result.required_history == 3);
    CPPTEST_ASSERT(calculated.result.outputs[0].samples.size() == 3);
    CPPTEST_ASSERT(calculated.result.outputs[0].samples[0].timestamp == input[2].open_time);
    CPPTEST_ASSERT(near(calculated.result.outputs[0].samples[0].value, 2.0));
    CPPTEST_ASSERT(near(calculated.result.outputs[0].samples[2].value, 4.0));

    const auto repeated = analyzer.calculate({configured, input, 1, std::nullopt});
    CPPTEST_ASSERT(repeated.recalculation == RecalculationKind::None);
    CPPTEST_ASSERT(repeated.result.revision == calculated.result.revision);
    return 0;
}

int test_bands_fixture() {
    TaLib::Analyzer analyzer;
    const auto input = bars({1, 2, 3, 4, 5});
    const auto calculated = analyzer.calculate({instance("bbands"), input, 1, std::nullopt});
    CPPTEST_ASSERT(calculated.result.outputs.size() == 3);
    CPPTEST_ASSERT(near(calculated.result.outputs[1].samples[0].value, 2.0));
    CPPTEST_ASSERT(near(calculated.result.outputs[0].samples[0].value, 2.0 + 2.0 * std::sqrt(2.0 / 3.0)));
    CPPTEST_ASSERT(near(calculated.result.outputs[2].samples[0].value, 2.0 - 2.0 * std::sqrt(2.0 / 3.0)));
    return 0;
}

int test_errors_insufficient_history_and_tail() {
    TaLib::Analyzer analyzer;
    auto configured = instance("sma");
    auto short_input = bars({1, 2});
    const auto insufficient = analyzer.calculate({configured, short_input, 1, std::nullopt});
    CPPTEST_ASSERT(insufficient.result.state == CalculationState::InsufficientHistory);
    CPPTEST_ASSERT(insufficient.result.required_history == 3);

    configured.parameters["period"] = std::int64_t{1};
    const auto invalid = analyzer.calculate({configured, short_input, 2, std::nullopt});
    CPPTEST_ASSERT(invalid.result.state == CalculationState::Error);

    configured.parameters["period"] = std::int64_t{3};
    auto input = bars({1, 2, 3, 4, 5});
    const auto initial = analyzer.calculate({configured, input, 3, std::nullopt});
    CPPTEST_ASSERT(initial.result.state == CalculationState::Ready);

    input.back().close = 8;
    const Range dirty{input.back().open_time, *input.back().close_time};
    const auto updated = analyzer.calculate({configured, input, 4, dirty});
    CPPTEST_ASSERT(updated.recalculation == RecalculationKind::Tail);
    CPPTEST_ASSERT(updated.result.revision == initial.result.revision + 1);
    CPPTEST_ASSERT(near(updated.result.outputs[0].samples.back().value, 5.0));
    return 0;
}

int main() {
    CPPTEST_RUN(test_catalog_metadata_and_visuals);
    CPPTEST_RUN(test_sma_alignment_gaps_and_state);
    CPPTEST_RUN(test_bands_fixture);
    CPPTEST_RUN(test_errors_insufficient_history_and_tail);
    return 0;
}
