#pragma once

#include <Didrachma/market/core/provider/History.h>
#include <string>

namespace Didrachma::Market::Providers::Yahoo::Intern {
std::string cache_filename(const Core::Provider::HistoryRequest& request);
} // namespace Didrachma::Market::Providers::Yahoo::Intern
