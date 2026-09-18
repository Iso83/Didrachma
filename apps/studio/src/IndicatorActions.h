#pragma once

#include <Didrachma/analysis/core/indicator/Definition.h>
#include <Didrachma/stockChart/core/Profile.h>

namespace Didrachma::Apps::Studio {
StockChart::Core::Profile capture_profile(const StockChart::Core::Document& document, std::string name);
std::string add_indicator(StockChart::Core::Document& document,
                          const Analysis::Core::Indicator::Definition& definition);
} // namespace Didrachma::Apps::Studio
