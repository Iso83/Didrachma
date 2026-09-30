#include "Catalog.h"
#include "Runtime.h"

#include <Didrachma/analysis/adapters/talib/Analyzer.h>
#include <Didrachma/analysis/core/indicator/Validation.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <ta_abstract.h>
#include <ta_libc.h>

using namespace Didrachma::Analysis::Core::Indicator;

namespace Didrachma::Analysis::Adapters::TaLib {
namespace Intern {
std::uint64_t input_fingerprint(std::span<const Market::Core::Series::Bar> bars) {
    std::uint64_t value = 1469598103934665603ULL;
    const auto append = [&](std::uint64_t item) {
        value ^= item;
        value *= 1099511628211ULL;
    };
    for (const auto& bar : bars) {
        append(static_cast<std::uint64_t>(bar.open_time.time_since_epoch().count()));
        append(bar.close_time ? static_cast<std::uint64_t>(bar.close_time->time_since_epoch().count()) : 0);
        for (const auto number : {bar.open, bar.high, bar.low, bar.close, bar.volume}) {
            std::uint64_t bits{};
            static_assert(sizeof(bits) == sizeof(number));
            std::memcpy(&bits, &number, sizeof(bits));
            append(bits);
        }
        append(static_cast<std::uint64_t>(bar.state));
    }
    return value;
}

void merge(OutputSeries& destination, OutputSeries calculated) {
    if (calculated.samples.empty())
        return;

    const auto begin = calculated.samples.front().timestamp;
    std::erase_if(destination.samples, [&](const auto& sample) { return sample.timestamp >= begin; });
    destination.samples.insert(destination.samples.end(), calculated.samples.begin(), calculated.samples.end());
}

struct Holder {
    TA_ParamHolder* value{};
    explicit Holder(const TA_FuncHandle* handle) {
        if (TA_ParamHolderAlloc(handle, &value) != TA_SUCCESS)
            value = nullptr;
    }
    ~Holder() {
        if (value)
            TA_ParamHolderFree(value);
    }
    Holder(const Holder&) = delete;
    Holder& operator=(const Holder&) = delete;
};

TA_RetCode configure_options(const Descriptor& descriptor, const Instance& instance, TA_ParamHolder* holder) {
    for (std::size_t i = 0; i < descriptor.options.size(); ++i) {
        const auto& option = descriptor.options[i];
        const auto found = instance.parameters.find(option.id);
        if (found == instance.parameters.end())
            return TA_BAD_PARAM;

        const auto code =
            option.integer
                ? TA_SetOptInputParamInteger(holder, static_cast<unsigned int>(i),
                                             static_cast<TA_Integer>(std::get<std::int64_t>(found->second)))
                : TA_SetOptInputParamReal(holder, static_cast<unsigned int>(i), std::get<double>(found->second));
        if (code != TA_SUCCESS)
            return code;
    }

    return TA_SUCCESS;
}

int lookback(const Descriptor& descriptor, const Instance& instance) {
    Holder holder{descriptor.handle};
    TA_Integer value{};
    if (!holder.value || configure_options(descriptor, instance, holder.value) != TA_SUCCESS ||
        TA_GetLookback(holder.value, &value) != TA_SUCCESS)
        return -1;

    return value;
}
} // namespace Intern

class Analyzer::Impl {
public:
    struct Cache {
        Result result;
        std::size_t input_size{};
        std::string definition_id;
        std::map<std::string, ParameterValue> parameters;
        std::uint64_t input_fingerprint{};
    };
    std::map<std::string, Cache> instances;
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

std::size_t Analyzer::required_history(const Instance& instance) const {
    const auto definitions = Intern::catalog();
    const auto definition = std::ranges::find(definitions, instance.definition_id, &Definition::id);
    if (definition == definitions.end() || validate_parameters(*definition, instance.parameters))
        return 1;

    const auto* adapter = Intern::descriptor(instance.definition_id);
    if (!adapter)
        return 1;

    const auto value = Intern::lookback(*adapter, instance);
    return value < 0 ? 1 : static_cast<std::size_t>(value + 1);
}

CalculationOutcome Analyzer::calculate(const CalculationRequest& request) {
    Intern::Runtime::instance();
    auto& cache = m_impl->instances[request.instance.id];
    const auto input_fingerprint = Intern::input_fingerprint(request.bars);
    const bool same_configuration =
        request.instance.definition_id == cache.definition_id && request.instance.parameters == cache.parameters;
    if (same_configuration && request.input_revision == cache.result.input_revision &&
        input_fingerprint == cache.input_fingerprint)
        return {cache.result, RecalculationKind::None};

    auto publish = [&](Result result, RecalculationKind mode, std::size_t input_begin = 0, std::size_t input_count = 0,
                       std::size_t reused_prefix = 0) {
        result.revision = cache.result.revision + 1;
        cache.result = result;
        cache.input_size = request.bars.size();
        cache.definition_id = request.instance.definition_id;
        cache.parameters = request.instance.parameters;
        cache.input_fingerprint = input_fingerprint;
        return CalculationOutcome{std::move(result), mode, input_begin, input_count, reused_prefix};
    };

    const auto definitions = Intern::catalog();
    const auto definition = std::ranges::find(definitions, request.instance.definition_id, &Definition::id);
    if (definition == definitions.end())
        return publish({{},
                        request.input_revision,
                        0,
                        CalculationState::Error,
                        CalculationError{CalculationErrorCode::UnknownDefinition, "Unknown indicator definition"}},
                       RecalculationKind::Full);
    if (const auto error = validate_parameters(*definition, request.instance.parameters))
        return publish({{},
                        request.input_revision,
                        0,
                        CalculationState::Error,
                        CalculationError{CalculationErrorCode::InvalidParameters, error->message}},
                       RecalculationKind::Full);

    const auto* adapter = Intern::descriptor(request.instance.definition_id);
    const auto lookback = Intern::lookback(*adapter, request.instance);
    if (request.bars.size() <= static_cast<std::size_t>(lookback))
        return publish({{},
                        request.input_revision,
                        0,
                        CalculationState::InsufficientHistory,
                        {},
                        static_cast<std::size_t>(lookback + 1)},
                       RecalculationKind::Full);

    RecalculationKind mode = RecalculationKind::Full;
    std::size_t start = 0;
    if (request.dirty_range && same_configuration && request.bars.size() >= cache.input_size &&
        !cache.result.outputs.empty() && adapter->definition.id != "ad" && adapter->definition.id != "adosc" &&
        adapter->definition.id != "obv" && adapter->definition.id != "sar" && adapter->definition.id != "sarext") {
        const auto dirty = std::ranges::lower_bound(request.bars, request.dirty_range->begin, {},
                                                    &Market::Core::Series::Bar::open_time);
        const auto dirty_index = static_cast<std::size_t>(std::distance(request.bars.begin(), dirty));
        start = request.dirty_range_includes_lookback
                    ? dirty_index
                    : (dirty_index > static_cast<std::size_t>(lookback) ? dirty_index - lookback : 0);
        mode = RecalculationKind::Tail;
        if (request.bars.size() - start <= static_cast<std::size_t>(lookback)) {
            start = 0;
            mode = RecalculationKind::Full;
        }
    }

    std::vector<double> open, high, low, close, volume;
    const auto requires_input = [&](MarketInput input) {
        return std::ranges::find(adapter->definition.inputs, input) != adapter->definition.inputs.end();
    };
    for (std::size_t i = start; i < request.bars.size(); ++i) {
        const auto& bar = request.bars[i];
        if ((requires_input(MarketInput::Open) && !std::isfinite(bar.open)) ||
            (requires_input(MarketInput::High) && !std::isfinite(bar.high)) ||
            (requires_input(MarketInput::Low) && !std::isfinite(bar.low)) ||
            (requires_input(MarketInput::Close) && !std::isfinite(bar.close)) ||
            (requires_input(MarketInput::Volume) && !std::isfinite(bar.volume)))
            return publish(
                {{},
                 request.input_revision,
                 0,
                 CalculationState::Error,
                 CalculationError{CalculationErrorCode::MissingInput, "Required market input is not finite"}},
                mode);
        open.push_back(bar.open);
        high.push_back(bar.high);
        low.push_back(bar.low);
        close.push_back(bar.close);
        volume.push_back(bar.volume);
    }

    Intern::Holder holder{adapter->handle};
    TA_RetCode code = holder.value ? Intern::configure_options(*adapter, request.instance, holder.value) : TA_ALLOC_ERR;
    for (std::size_t i = 0; code == TA_SUCCESS && i < adapter->inputs.size(); ++i) {
        const auto& input = adapter->inputs[i];
        code = input.kind == Intern::Input::Kind::Real
                   ? TA_SetInputParamRealPtr(holder.value, static_cast<unsigned int>(i), close.data())
                   : TA_SetInputParamPricePtr(holder.value, static_cast<unsigned int>(i), open.data(), high.data(),
                                              low.data(), close.data(), volume.data(), nullptr);
    }

    std::vector<std::vector<double>> real_outputs(adapter->outputs.size(), std::vector<double>(close.size()));
    std::vector<std::vector<TA_Integer>> integer_outputs(adapter->outputs.size(),
                                                         std::vector<TA_Integer>(close.size()));
    for (std::size_t i = 0; code == TA_SUCCESS && i < adapter->outputs.size(); ++i)
        code = adapter->outputs[i].integer
                   ? TA_SetOutputParamIntegerPtr(holder.value, static_cast<unsigned int>(i), integer_outputs[i].data())
                   : TA_SetOutputParamRealPtr(holder.value, static_cast<unsigned int>(i), real_outputs[i].data());

    TA_Integer output_begin = 0, output_count = 0;
    if (code == TA_SUCCESS)
        code = TA_CallFunc(holder.value, 0, static_cast<TA_Integer>(close.size()) - 1, &output_begin, &output_count);
    std::vector<OutputSeries> calculated;
    calculated.reserve(adapter->outputs.size());
    for (std::size_t output_index = 0; output_index < adapter->outputs.size(); ++output_index) {
        calculated.push_back({adapter->outputs[output_index].id, {}});
        for (TA_Integer i = 0; i < output_count; ++i) {
            const auto value = adapter->outputs[output_index].integer
                                   ? static_cast<double>(integer_outputs[output_index][i])
                                   : real_outputs[output_index][i];
            calculated.back().samples.push_back(
                {request.bars[start + static_cast<std::size_t>(output_begin + i)].open_time, value});
        }
    }
    if (code != TA_SUCCESS)
        return publish({{},
                        request.input_revision,
                        0,
                        CalculationState::Error,
                        CalculationError{CalculationErrorCode::LibraryFailure,
                                         "TA-Lib calculation failed with code " + std::to_string(code)}},
                       mode);

    std::size_t reused_prefix = 0;
    if (mode == RecalculationKind::Tail && !calculated.empty() && !calculated.front().samples.empty() &&
        !cache.result.outputs.empty())
        reused_prefix = static_cast<std::size_t>(
            std::ranges::count_if(cache.result.outputs.front().samples, [&](const auto& sample) {
                return sample.timestamp < calculated.front().samples.front().timestamp;
            }));
    Result result = mode == RecalculationKind::Tail ? cache.result : Result{};
    if (mode == RecalculationKind::Tail)
        for (auto& output : calculated) {
            auto destination = std::ranges::find(result.outputs, output.output_id, &OutputSeries::output_id);
            Intern::merge(*destination, std::move(output));
        }
    else
        result.outputs = std::move(calculated);

    result.input_revision = request.input_revision;
    result.state = CalculationState::Ready;
    result.error.reset();
    result.required_history = static_cast<std::size_t>(lookback + 1);
    return publish(std::move(result), mode, start, request.bars.size() - start, reused_prefix);
}
} // namespace Didrachma::Analysis::Adapters::TaLib
