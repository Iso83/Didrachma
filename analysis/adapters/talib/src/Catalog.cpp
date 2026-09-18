#include "Catalog.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <ta_abstract.h>

using namespace Didrachma::Analysis::Core::Indicator;

namespace Didrachma::Analysis::Adapters::TaLib::Intern {
namespace {
std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string option_id(std::string name) {
    if (name.starts_with("optIn"))
        name.erase(0, 5);
    if (name == "TimePeriod")
        return "period";
    if (name == "NbDevUp" || name == "NbDevDn")
        return "deviation";
    if (name == "Acceleration")
        return "acceleration";
    if (name == "Maximum")
        return "maximum";
    if (name == "MAType")
        return "ma_type";
    std::string id;
    for (const unsigned char c : name) {
        if (std::isupper(c) && !id.empty())
            id += '_';
        id += static_cast<char>(std::tolower(c));
    }
    return id;
}

std::string output_id(std::string name, unsigned int count) {
    if (name.starts_with("out"))
        name.erase(0, 3);
    if (count == 1)
        return "value";
    if (name.starts_with("Real"))
        name.erase(0, 4);
    return lower(std::move(name));
}

void append(const TA_FuncInfo* info, void* opaque) {
    auto& result = *static_cast<std::vector<Descriptor>*>(opaque);
    const std::string_view group{info->group};
    if (group == "Math Operators" || group == "Math Transform")
        return;

    Descriptor descriptor;
    descriptor.handle = info->handle;
    descriptor.ta_name = info->name;
    descriptor.flags = info->flags;
    auto& definition = descriptor.definition;
    definition.id = lower(info->name);
    definition.display_name = info->hint;
    definition.group = info->group;
    definition.pane = (info->flags & TA_FUNC_FLG_OVERLAP) ? PaneHint::PriceOverlay : PaneHint::Separate;
    if (group == "Pattern Recognition")
        definition.capability = Capability::AnalysisEvent;

    unsigned int real_inputs = 0;
    for (unsigned int i = 0; i < info->nbInput; ++i) {
        const TA_InputParameterInfo* input{};
        if (TA_GetInputParameterInfo(info->handle, i, &input) != TA_SUCCESS || input->type == TA_Input_Integer)
            return;

        if (input->type == TA_Input_Real && ++real_inputs > 1)
            return; // No provider-neutral mapping exists for an arbitrary second real series.

        descriptor.inputs.push_back(
            {input->type == TA_Input_Price ? Input::Kind::Price : Input::Kind::Real, input->flags});
        const auto add = [&](MarketInput value) {
            if (std::ranges::find(definition.inputs, value) == definition.inputs.end())
                definition.inputs.push_back(value);
        };
        if (input->type == TA_Input_Real)
            add(MarketInput::Close);
        if (input->flags & TA_IN_PRICE_OPEN)
            add(MarketInput::Open);
        if (input->flags & TA_IN_PRICE_HIGH)
            add(MarketInput::High);
        if (input->flags & TA_IN_PRICE_LOW)
            add(MarketInput::Low);
        if (input->flags & TA_IN_PRICE_CLOSE)
            add(MarketInput::Close);
        if (input->flags & TA_IN_PRICE_VOLUME)
            add(MarketInput::Volume);
        if (input->flags & TA_IN_PRICE_OPENINTEREST)
            return;
    }

    std::set<std::string> option_ids;
    for (unsigned int i = 0; i < info->nbOptInput; ++i) {
        const TA_OptInputParameterInfo* option{};
        if (TA_GetOptInputParameterInfo(info->handle, i, &option) != TA_SUCCESS)
            return;
        const bool integer = option->type == TA_OptInput_IntegerRange || option->type == TA_OptInput_IntegerList;
        const auto id = option_id(option->paramName);
        descriptor.options.push_back({id, integer});
        if (!option_ids.insert(id).second)
            continue;
        std::optional<double> minimum, maximum;
        if (option->type == TA_OptInput_IntegerRange) {
            const auto* range = static_cast<const TA_IntegerRange*>(option->dataSet);
            minimum = range->min;
            maximum = range->max;
        } else if (option->type == TA_OptInput_RealRange) {
            const auto* range = static_cast<const TA_RealRange*>(option->dataSet);
            minimum = range->min;
            maximum = range->max;
        }
        definition.parameters.push_back({id, option->displayName,
                                         integer ? ParameterKind::Integer : ParameterKind::Real,
                                         integer ? ParameterValue{static_cast<std::int64_t>(option->defaultValue)}
                                                 : ParameterValue{option->defaultValue},
                                         minimum, maximum});
    }

    for (unsigned int i = 0; i < info->nbOutput; ++i) {
        const TA_OutputParameterInfo* output{};
        if (TA_GetOutputParameterInfo(info->handle, i, &output) != TA_SUCCESS)
            return;
        const auto id = output_id(output->paramName, info->nbOutput);
        const bool pattern = output->flags & (TA_OUT_PATTERN_BOOL | TA_OUT_PATTERN_BULL_BEAR | TA_OUT_PATTERN_STRENGTH);
        const auto visual =
            pattern ? VisualKind::Marker : (output->flags & TA_OUT_HISTO ? VisualKind::Histogram : VisualKind::Line);
        auto role = OutputRole::Value;
        if (output->flags & TA_OUT_UPPER_LIMIT)
            role = OutputRole::UpperBand;
        else if (output->flags & TA_OUT_LOWER_LIMIT)
            role = OutputRole::LowerBand;
        else if (definition.pane == PaneHint::PriceOverlay)
            role = OutputRole::PriceReference;
        definition.outputs.push_back({id, output->paramName, visual, role});
        descriptor.outputs.push_back({id, output->type == TA_Output_Integer});
    }

    if (definition.id == "bbands") {
        definition.outputs[0].id = descriptor.outputs[0].id = "upper";
        definition.outputs[1].id = descriptor.outputs[1].id = "middle";
        definition.outputs[2].id = descriptor.outputs[2].id = "lower";
        definition.outputs[0].visual = definition.outputs[2].visual = VisualKind::Band;
        definition.outputs[0].role = OutputRole::UpperBand;
        definition.outputs[1].role = OutputRole::MiddleBand;
        definition.outputs[2].role = OutputRole::LowerBand;
    }
    if (definition.id == "rsi") {
        definition.range = RangeHint::Fixed;
        definition.range_min = 0.0;
        definition.range_max = 100.0;
    }
    result.push_back(std::move(descriptor));
}

std::vector<Descriptor> build_descriptors() {
    std::vector<Descriptor> result;
    TA_ForEachFunc(append, &result);
    std::ranges::sort(result, {}, [](const Descriptor& value) { return value.definition.group + value.ta_name; });
    return result;
}
} // namespace

const std::vector<Descriptor>& descriptors() {
    static const auto values = build_descriptors();
    return values;
}

std::vector<Definition> catalog() {
    std::vector<Definition> result;
    result.reserve(descriptors().size());
    for (const auto& value : descriptors())
        result.push_back(value.definition);
    return result;
}

const Descriptor* descriptor(std::string_view id) {
    const auto found = std::ranges::find(descriptors(), id,
                                         [](const Descriptor& value) { return std::string_view{value.definition.id}; });
    return found == descriptors().end() ? nullptr : &*found;
}
} // namespace Didrachma::Analysis::Adapters::TaLib::Intern
