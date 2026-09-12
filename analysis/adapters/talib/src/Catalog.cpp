#include "Catalog.h"

#include <ta_abstract.h>

using namespace Didrachma::Analysis::Core::Indicator;

namespace Didrachma::Analysis::Adapters::TaLib::Intern {
Definition sma_definition() {
    return {"sma",
            "Simple Moving Average",
            {{"period", "Period", ParameterKind::Integer, std::int64_t{20}, 2, 100000}},
            {{"value", "SMA", VisualKind::Line}}};
}

Definition bands_definition() {
    return {"bbands",
            "Bollinger Bands",
            {{"period", "Period", ParameterKind::Integer, std::int64_t{20}, 2, 100000},
             {"deviation", "Deviation", ParameterKind::Real, 2.0, 0.01, 1000.0}},
            {{"upper", "Upper", VisualKind::Band},
             {"middle", "Middle", VisualKind::Line},
             {"lower", "Lower", VisualKind::Band}}};
}

std::vector<Definition> catalog() {
    std::vector<Definition> result;
    const TA_FuncHandle* handle = nullptr;
    const TA_FuncInfo* info = nullptr;

    if (TA_GetFuncHandle("SMA", &handle) == TA_SUCCESS && TA_GetFuncInfo(handle, &info) == TA_SUCCESS) {
        auto definition = sma_definition();
        definition.display_name = info->hint;
        result.push_back(std::move(definition));
    }

    if (TA_GetFuncHandle("BBANDS", &handle) == TA_SUCCESS && TA_GetFuncInfo(handle, &info) == TA_SUCCESS) {
        auto definition = bands_definition();
        definition.display_name = info->hint;
        result.push_back(std::move(definition));
    }

    return result;
}
} // namespace Didrachma::Analysis::Adapters::TaLib::Intern
