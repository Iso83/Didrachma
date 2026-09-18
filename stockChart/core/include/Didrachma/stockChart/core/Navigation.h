#pragma once

#include <Didrachma/market/core/time/Range.h>
#include <optional>
#include <string>
#include <variant>

namespace Didrachma::StockChart::Core {
struct NavigateViewport {
    Market::Core::Time::Range range;
};

struct SelectTimestamp {
    std::optional<Market::Core::Time::UtcTimestamp> timestamp;
};

struct SelectRange {
    std::optional<Market::Core::Time::Range> range;
};

struct SelectAnalysisEvent {
    std::optional<std::string> event_id;
};

using NavigationCommand = std::variant<NavigateViewport, SelectTimestamp, SelectRange, SelectAnalysisEvent>;

enum class NavigationEventKind { ViewportChanged, TimestampSelected, RangeSelected, AnalysisEventSelected };

struct NavigationEvent {
    NavigationEventKind kind;
};
} // namespace Didrachma::StockChart::Core
