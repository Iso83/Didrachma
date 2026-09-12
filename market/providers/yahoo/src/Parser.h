#pragma once

#include <Didrachma/market/core/series/Bar.h>
#include <nlohmann/json_fwd.hpp>
#include <vector>

namespace Didrachma::Market::Providers::Yahoo::Intern {
std::vector<Core::Series::Bar> parse_chart(const nlohmann::json& json);
} // namespace Didrachma::Market::Providers::Yahoo::Intern
