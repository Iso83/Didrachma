#pragma once

#include <Didrachma/analysis/core/indicator/Definition.h>
#include <Didrachma/stockChart/core/Document.h>

namespace Didrachma::StockChart::Core {
bool reconcile_pattern_projection(Document& document, const Analysis::Core::Indicator::Definition& definition,
                                  const std::string& instance_id);
} // namespace Didrachma::StockChart::Core
