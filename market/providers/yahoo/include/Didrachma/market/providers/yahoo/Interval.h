#pragma once

#include <Didrachma/market/core/time/Frame.h>
#include <Didrachma/market/core/time/Range.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Didrachma::Market::Providers::Yahoo {
struct Interval {
    Core::Time::Frame frame;
    Core::Time::Frame source_frame;
    std::string_view value;
    std::string_view display_name;
    std::optional<std::chrono::days> history_reach;

    [[nodiscard]] bool derived() const {
        return frame != source_frame;
    }
};

[[nodiscard]] std::span<const Interval> intervals();
[[nodiscard]] const Interval* find_interval(Core::Time::Frame frame);

struct HistoryValidation {
    bool valid{};
    std::string message;
    std::optional<Core::Time::UtcTimestamp> earliest_allowed;
};

[[nodiscard]] HistoryValidation validate_history(Core::Time::Frame frame, Core::Time::Range range,
                                                 Core::Time::UtcTimestamp now);
[[nodiscard]] Core::Time::Range suggested_history_range(Core::Time::Frame frame, Core::Time::UtcTimestamp now);
} // namespace Didrachma::Market::Providers::Yahoo
