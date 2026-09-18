#include "TestAssert.h"

#include <Didrachma/analysis/adapters/talib/Analyzer.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

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
    return {"one",
            std::move(definition),
            true,
            {{"period", std::int64_t{3}}, {"deviation", deviation}, {"ma_type", std::int64_t{0}}}};
}

bool near(double left, double right) {
    return std::abs(left - right) < 1e-9;
}

int test_catalog_metadata_and_visuals() {
    TaLib::Analyzer analyzer;
    const auto catalog = analyzer.catalog();
    const auto find = [&](std::string_view id) -> const Definition* {
        const auto found = std::ranges::find(catalog, id, &Definition::id);
        return found == catalog.end() ? nullptr : &*found;
    };
    CPPTEST_ASSERT(catalog.size() > 100);
    CPPTEST_ASSERT(find("sma"));
    const auto& sma = *find("sma");
    CPPTEST_ASSERT(sma.display_name == "Simple Moving Average");
    CPPTEST_ASSERT(sma.parameters[0].id == "period" && sma.parameters[0].kind == ParameterKind::Integer);
    CPPTEST_ASSERT(sma.outputs[0].id == "value" && sma.outputs[0].visual == VisualKind::Line);

    for (const auto* id : {"adx",      "adxr",  "aroon",   "cci",      "cmo",  "dx",     "macd", "mfi",
                           "minus_di", "mom",   "plus_di", "ppo",      "roc",  "rocp",   "rocr", "rocr100",
                           "rsi",      "stoch", "stochf",  "stochrsi", "trix", "ultosc", "willr"})
        CPPTEST_ASSERT(find(id)->group == "Momentum Indicators");
    for (const auto* id : {"atr", "natr", "trange"})
        CPPTEST_ASSERT(find(id)->group == "Volatility Indicators");
    for (const auto* id : {"ad", "adosc", "obv"}) {
        CPPTEST_ASSERT(find(id)->group == "Volume Indicators");
        CPPTEST_ASSERT(find(id)->pane == PaneHint::Separate);
    }
    for (const auto* id : {"bbands", "dema", "ema", "ht_trendline", "kama", "ma", "mama", "midpoint", "midprice", "sar",
                           "sarext", "sma", "t3", "tema", "trima", "wma"})
        CPPTEST_ASSERT(find(id)->pane == PaneHint::PriceOverlay);
    for (const auto* id : {"ht_dcperiod", "ht_dcphase", "ht_phasor", "ht_sine", "ht_trendmode", "avgprice", "medprice",
                           "typprice", "wclprice", "linearreg", "stddev", "tsf", "var"})
        CPPTEST_ASSERT(find(id));

    const auto& bands = *find("bbands");
    CPPTEST_ASSERT(bands.outputs.size() == 3 && bands.outputs[0].id == "upper" && bands.outputs[2].id == "lower");
    CPPTEST_ASSERT(bands.outputs[0].visual == VisualKind::Band && bands.outputs[1].visual == VisualKind::Line);
    const auto& macd = *find("macd");
    CPPTEST_ASSERT(macd.outputs.size() == 3 && macd.parameters.size() == 3);
    CPPTEST_ASSERT(find("cdlengulfing")->capability == Capability::AnalysisEvent);
    return 0;
}

int test_expanded_family_fixtures_and_inputs() {
    TaLib::Analyzer analyzer;
    auto input = bars({1, 2, 4, 7, 11, 16, 22, 29, 37, 46, 56, 67, 79, 92, 106, 121});
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i].high = input[i].close + 2;
        input[i].low = input[i].close - 1;
        input[i].volume = static_cast<double>(100 + i);
    }
    for (const auto* id : {"midpoint", "midprice", "sar", "mom", "roc", "rsi", "atr", "natr", "trange", "obv"}) {
        auto configured = instance(id);
        configured.parameters["period"] = std::int64_t{3};
        configured.parameters["acceleration"] = 0.02;
        configured.parameters["maximum"] = 0.2;
        const auto calculated = analyzer.calculate({configured, input, 1, std::nullopt});
        CPPTEST_ASSERT(calculated.result.state == CalculationState::Ready);
        CPPTEST_ASSERT(calculated.result.outputs.size() == 1);
        CPPTEST_ASSERT(!calculated.result.outputs[0].samples.empty());
        if (id == std::string_view{"obv"})
            CPPTEST_ASSERT(calculated.result.outputs[0].samples.size() == input.size());
        if (id == std::string_view{"roc"})
            CPPTEST_ASSERT(calculated.result.outputs[0].samples.size() == input.size() - 3);
        CPPTEST_ASSERT(calculated.result.outputs[0].samples.back().timestamp == input.back().open_time);
    }

    input[4].high = std::numeric_limits<double>::quiet_NaN();
    const auto missing = analyzer.calculate({instance("atr"), input, 2, std::nullopt});
    CPPTEST_ASSERT(missing.result.error->code == CalculationErrorCode::MissingInput);
    return 0;
}

int test_configuration_and_instance_cache_identity() {
    TaLib::Analyzer analyzer;
    const auto input = bars({1, 2, 3, 4, 5});
    auto first = instance("sma");
    const auto initial = analyzer.calculate({first, input, 1, std::nullopt});
    first.parameters["period"] = std::int64_t{4};
    const auto changed = analyzer.calculate({first, input, 1, std::nullopt});
    CPPTEST_ASSERT(changed.recalculation == RecalculationKind::Full);
    CPPTEST_ASSERT(changed.result.revision == initial.result.revision + 1);
    CPPTEST_ASSERT(changed.result.outputs[0].samples.size() == 2);

    auto second = instance("sma");
    second.id = "two";
    const auto other = analyzer.calculate({second, input, 1, std::nullopt});
    CPPTEST_ASSERT(other.result.revision == 1);
    const auto first_again = analyzer.calculate({first, input, 1, std::nullopt});
    CPPTEST_ASSERT(first_again.recalculation == RecalculationKind::None);
    return 0;
}

int test_update_modes_and_state_revisions() {
    TaLib::Analyzer analyzer;
    auto configured = instance("sma");
    auto input = bars({1, 2, 3, 4});
    const auto initial = analyzer.calculate({configured, input, 1, std::nullopt});
    input.push_back({at(5), at(6), 5, 5, 5, 5, 1, BarState::Closed});
    const auto appended = analyzer.calculate({configured, input, 2, Range{at(5), at(6)}});
    CPPTEST_ASSERT(appended.recalculation == RecalculationKind::Tail);
    input.back().state = BarState::Forming;
    input.back().close = 8;
    const auto forming = analyzer.calculate({configured, input, 3, Range{at(5), at(6)}});
    CPPTEST_ASSERT(forming.recalculation == RecalculationKind::Tail);
    CPPTEST_ASSERT(near(forming.result.outputs[0].samples.back().value, 5.0));
    input.resize(4);
    const auto fallback = analyzer.calculate({configured, input, 4, Range{at(3), at(4)}});
    CPPTEST_ASSERT(fallback.recalculation == RecalculationKind::Full);
    input.resize(2);
    const auto insufficient = analyzer.calculate({configured, input, 5, std::nullopt});
    CPPTEST_ASSERT(insufficient.result.revision == fallback.result.revision + 1);
    configured.parameters["period"] = std::int64_t{1};
    const auto error = analyzer.calculate({configured, input, 5, std::nullopt});
    CPPTEST_ASSERT(error.result.revision == insufficient.result.revision + 1);
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

int test_additional_moving_average_fixtures() {
    TaLib::Analyzer analyzer;
    const auto input = bars({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
    const auto ema = analyzer.calculate({instance("ema"), input, 1, std::nullopt});
    const auto wma = analyzer.calculate({instance("wma"), input, 1, std::nullopt});
    CPPTEST_ASSERT(near(ema.result.outputs[0].samples.front().value, 2.0));
    CPPTEST_ASSERT(near(ema.result.outputs[0].samples.back().value, 9.0));
    CPPTEST_ASSERT(near(wma.result.outputs[0].samples.front().value, 14.0 / 6.0));
    CPPTEST_ASSERT(near(wma.result.outputs[0].samples.back().value, 56.0 / 6.0));

    const auto longer = bars({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20});
    for (const auto* definition : {"dema", "tema", "trima", "kama"}) {
        auto configured = instance(definition);
        const auto calculated = analyzer.calculate({configured, longer, 1, std::nullopt});
        CPPTEST_ASSERT(calculated.result.state == CalculationState::Ready);
        CPPTEST_ASSERT(!calculated.result.outputs[0].samples.empty());
    }
    return 0;
}

int test_errors_insufficient_history_and_tail() {
    TaLib::Analyzer analyzer;
    auto configured = instance("sma");
    auto short_input = bars({1, 2});
    const auto insufficient = analyzer.calculate({configured, short_input, 1, std::nullopt});
    CPPTEST_ASSERT(insufficient.result.state == CalculationState::InsufficientHistory);
    CPPTEST_ASSERT(insufficient.result.required_history == 3);

    configured.parameters.erase("period");
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

int test_generic_multi_output_and_parameters() {
    TaLib::Analyzer analyzer;
    const auto catalog = analyzer.catalog();
    const auto definition = std::ranges::find(catalog, std::string{"macd"}, &Definition::id);
    CPPTEST_ASSERT(definition != catalog.end());
    std::map<std::string, ParameterValue> parameters;
    for (const auto& parameter : definition->parameters)
        parameters.emplace(parameter.id, parameter.default_value);

    std::vector<Bar> input;
    for (int i = 0; i < 80; ++i)
        input.push_back({at(i), at(i + 1), 100.0 + i, 102.0 + i, 99.0 + i, 101.0 + i, 1000.0 + i, BarState::Closed});
    const auto calculated = analyzer.calculate({{"macd-one", "macd", true, parameters}, input, 1, std::nullopt});
    CPPTEST_ASSERT(calculated.result.state == CalculationState::Ready);
    CPPTEST_ASSERT(calculated.result.outputs.size() == 3);
    CPPTEST_ASSERT(std::all_of(calculated.result.outputs.begin(), calculated.result.outputs.end(),
                               [](const auto& output) { return !output.samples.empty(); }));
    return 0;
}

int main() {
    CPPTEST_RUN(test_catalog_metadata_and_visuals);
    CPPTEST_RUN(test_sma_alignment_gaps_and_state);
    CPPTEST_RUN(test_bands_fixture);
    CPPTEST_RUN(test_additional_moving_average_fixtures);
    CPPTEST_RUN(test_errors_insufficient_history_and_tail);
    CPPTEST_RUN(test_configuration_and_instance_cache_identity);
    CPPTEST_RUN(test_update_modes_and_state_revisions);
    CPPTEST_RUN(test_expanded_family_fixtures_and_inputs);
    CPPTEST_RUN(test_generic_multi_output_and_parameters);
    return 0;
}
