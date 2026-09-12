#include "Catalog.h"
#include "Runtime.h"

#include <Didrachma/analysis/adapters/talib/Analyzer.h>
#include <Didrachma/analysis/core/indicator/Validation.h>
#include <algorithm>
#include <ta_abstract.h>
#include <ta_libc.h>

using namespace Didrachma::Analysis::Core::Indicator;

namespace Didrachma::Analysis::Adapters::TaLib {
std::int64_t integer_parameter(const Instance& instance, const std::string& id) {
    const auto at = instance.parameters.find(id);
    return at == instance.parameters.end() ? 0 : std::get<std::int64_t>(at->second);
}

double real_parameter(const Instance& instance, const std::string& id) {
    const auto at = instance.parameters.find(id);
    return at == instance.parameters.end() ? 0.0 : std::get<double>(at->second);
}

void merge(OutputSeries& destination, OutputSeries calculated) {
    if (calculated.samples.empty())
        return;

    const auto begin = calculated.samples.front().timestamp;
    std::erase_if(destination.samples, [&](const auto& sample) { return sample.timestamp >= begin; });
    destination.samples.insert(destination.samples.end(), calculated.samples.begin(), calculated.samples.end());
}

class Analyzer::Impl {
public:
    Result cached;
    std::size_t input_size{};
    std::string instance_id;
    std::string definition_id;
    std::map<std::string, ParameterValue> parameters;
};

Analyzer::Analyzer() : m_impl(std::make_unique<Impl>()) {
    Intern::Runtime::instance();
}
Analyzer::~Analyzer() = default;
Analyzer::Analyzer(Analyzer&&) noexcept = default;
Analyzer& Analyzer::operator=(Analyzer&&) noexcept = default;

std::vector<Definition> Analyzer::catalog() const {
    Intern::Runtime::instance();
    return Intern::catalog();
}

CalculationOutcome Analyzer::calculate(const CalculationRequest& request) {
    Intern::Runtime::instance();
    if (request.input_revision == m_impl->cached.input_revision && request.instance.id == m_impl->instance_id)
        return {m_impl->cached, RecalculationKind::None};

    const auto definitions = Intern::catalog();
    const auto definition = std::ranges::find(definitions, request.instance.definition_id, &Definition::id);
    if (definition == definitions.end())
        return {{{},
                 request.input_revision,
                 m_impl->cached.revision + 1,
                 CalculationState::Error,
                 CalculationError{CalculationErrorCode::UnknownDefinition, "Unknown indicator definition"}},
                RecalculationKind::Full};
    if (const auto error = validate_parameters(*definition, request.instance.parameters))
        return {{{},
                 request.input_revision,
                 m_impl->cached.revision + 1,
                 CalculationState::Error,
                 CalculationError{CalculationErrorCode::InvalidParameters, error->message}},
                RecalculationKind::Full};

    const auto period = static_cast<int>(integer_parameter(request.instance, "period"));
    const auto lookback = request.instance.definition_id == "sma"
                              ? TA_SMA_Lookback(period)
                              : TA_BBANDS_Lookback(period, real_parameter(request.instance, "deviation"),
                                                   real_parameter(request.instance, "deviation"), TA_MAType_SMA);
    if (request.bars.size() <= static_cast<std::size_t>(lookback))
        return {{{},
                 request.input_revision,
                 m_impl->cached.revision + 1,
                 Core::Indicator::CalculationState::InsufficientHistory,
                 {},
                 static_cast<std::size_t>(lookback + 1)},
                RecalculationKind::Full};

    RecalculationKind mode = RecalculationKind::Full;
    std::size_t start = 0;
    if (request.dirty_range && request.instance.id == m_impl->instance_id &&
        request.instance.definition_id == m_impl->definition_id && request.instance.parameters == m_impl->parameters &&
        request.bars.size() >= m_impl->input_size && !m_impl->cached.outputs.empty()) {
        const auto dirty = std::ranges::lower_bound(request.bars, request.dirty_range->begin, {},
                                                    &Market::Core::Series::Bar::open_time);
        const auto dirty_index = static_cast<std::size_t>(std::distance(request.bars.begin(), dirty));
        start = dirty_index > static_cast<std::size_t>(lookback) ? dirty_index - lookback : 0;
        mode = RecalculationKind::Tail;
    }

    std::vector<double> input;
    input.reserve(request.bars.size() - start);
    for (std::size_t i = start; i < request.bars.size(); ++i)
        input.push_back(request.bars[i].close);

    int output_begin = 0;
    int output_count = 0;
    std::vector<OutputSeries> calculated;
    TA_RetCode code = TA_SUCCESS;
    if (request.instance.definition_id == "sma") {
        std::vector<double> values(input.size());
        code = TA_SMA(0, static_cast<int>(input.size()) - 1, input.data(), period, &output_begin, &output_count,
                      values.data());
        calculated = {{"value", {}}};
        for (int i = 0; i < output_count; ++i)
            calculated[0].samples.push_back({request.bars[start + output_begin + i].open_time, values[i]});
    } else {
        std::vector<double> upper(input.size()), middle(input.size()), lower(input.size());
        const auto deviation = real_parameter(request.instance, "deviation");
        code = TA_BBANDS(0, static_cast<int>(input.size()) - 1, input.data(), period, deviation, deviation,
                         TA_MAType_SMA, &output_begin, &output_count, upper.data(), middle.data(), lower.data());
        calculated = {{"upper", {}}, {"middle", {}}, {"lower", {}}};
        for (int i = 0; i < output_count; ++i) {
            const auto timestamp = request.bars[start + output_begin + i].open_time;
            calculated[0].samples.push_back({timestamp, upper[i]});
            calculated[1].samples.push_back({timestamp, middle[i]});
            calculated[2].samples.push_back({timestamp, lower[i]});
        }
    }

    if (code != TA_SUCCESS)
        return {{{},
                 request.input_revision,
                 m_impl->cached.revision + 1,
                 CalculationState::Error,
                 CalculationError{CalculationErrorCode::LibraryFailure,
                                  "TA-Lib calculation failed with code " + std::to_string(code)}},
                mode};

    Result result = mode == RecalculationKind::Tail ? m_impl->cached : Result{};
    if (mode == RecalculationKind::Tail)
        for (auto& output : calculated) {
            auto destination = std::ranges::find(result.outputs, output.output_id, &OutputSeries::output_id);
            merge(*destination, std::move(output));
        }
    else
        result.outputs = std::move(calculated);

    result.input_revision = request.input_revision;
    result.revision = m_impl->cached.revision + 1;
    result.state = CalculationState::Ready;
    result.error.reset();
    result.required_history = static_cast<std::size_t>(lookback + 1);

    m_impl->cached = result;
    m_impl->input_size = request.bars.size();
    m_impl->instance_id = request.instance.id;
    m_impl->definition_id = request.instance.definition_id;
    m_impl->parameters = request.instance.parameters;

    return {std::move(result), mode};
}
} // namespace Didrachma::Analysis::Adapters::TaLib
