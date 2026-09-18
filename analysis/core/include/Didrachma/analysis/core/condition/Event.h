#pragma once

#include <Didrachma/market/core/series/Key.h>
#include <Didrachma/market/core/time/Range.h>
#include <map>
#include <optional>
#include <string>

namespace Didrachma::Analysis::Core::Condition {
enum class Direction { Upward, Downward, Neutral };
enum class Severity { Information, Attention, Critical };

struct Event {
    std::string id;
    std::string condition_id;
    Market::Core::Series::Key series;
    Market::Core::Time::UtcTimestamp start;
    std::optional<Market::Core::Time::UtcTimestamp> end;
    Direction direction{Direction::Neutral};
    Severity severity{Severity::Information};
    std::string title;
    std::string summary;
    std::map<std::string, double> evidence;
    std::string chart_id;
    std::string source_instance_id;
    std::string source_name;
    std::string source_definition_id;
    std::map<std::string, std::string> source_parameters;
};
} // namespace Didrachma::Analysis::Core::Condition
