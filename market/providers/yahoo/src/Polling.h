#pragma once

#include <Didrachma/market/core/series/BarUpdate.h>
#include <optional>
#include <span>
#include <vector>

namespace Didrachma::Market::Providers::Yahoo::Intern {
std::vector<Core::Series::BarUpdate> polling_updates(const Core::Series::Key& key,
                                                     std::optional<Core::Series::Bar>& previous,
                                                     std::span<const Core::Series::Bar> bars,
                                                     Core::Time::UtcTimestamp now);
Core::Time::Range polling_range(Core::Time::Frame frame, Core::Time::UtcTimestamp now);
} // namespace Didrachma::Market::Providers::Yahoo::Intern
