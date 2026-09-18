#include "Polling.h"

#include <algorithm>

namespace Didrachma::Market::Providers::Yahoo::Intern {
namespace {
Core::Time::UtcTimestamp::duration duration(Core::Time::Frame frame) {
    using enum Core::Time::Unit;
    switch (frame.unit) {
        case Minute:
            return std::chrono::minutes{frame.quantity};
        case Hour:
            return std::chrono::hours{frame.quantity};
        case Day:
            return std::chrono::days{frame.quantity};
    }
    return {};
}

bool same_values(const Core::Series::Bar& left, const Core::Series::Bar& right) {
    return left.open == right.open && left.high == right.high && left.low == right.low && left.close == right.close &&
           left.volume == right.volume && left.close_time == right.close_time && left.state == right.state;
}

std::vector<Core::Series::Bar> normalize_buckets(Core::Time::Frame frame, std::span<const Core::Series::Bar> bars) {
    if (bars.empty())
        return {};

    const auto frame_duration = duration(frame);
    const auto anchor = bars.front().open_time;
    std::vector<Core::Series::Bar> normalized;
    normalized.reserve(bars.size());
    for (auto bar : bars) {
        const auto elapsed = bar.open_time - anchor;
        const auto bucket_count = elapsed / frame_duration;
        bar.open_time = anchor + frame_duration * bucket_count;
        bar.close_time = bar.open_time + frame_duration;
        if (!normalized.empty() && normalized.back().open_time == bar.open_time) {
            auto& bucket = normalized.back();
            bucket.high = std::max(bucket.high, bar.high);
            bucket.low = std::min(bucket.low, bar.low);
            bucket.close = bar.close;
            bucket.volume += bar.volume;
        } else
            normalized.push_back(std::move(bar));
    }
    return normalized;
}
} // namespace

Core::Time::Range polling_range(Core::Time::Frame frame, Core::Time::UtcTimestamp now) {
    const auto frame_duration = duration(frame);
    const auto lookback = std::max(frame_duration * 3, Core::Time::UtcTimestamp::duration{std::chrono::hours{24}});
    return {now - lookback, now + frame_duration};
}

std::vector<Core::Series::BarUpdate> polling_updates(const Core::Series::Key& key,
                                                     std::optional<Core::Series::Bar>& previous,
                                                     std::span<const Core::Series::Bar> bars,
                                                     Core::Time::UtcTimestamp now) {
    auto normalized = normalize_buckets(key.timeframe, bars);
    if (normalized.empty())
        return {};

    auto latest = normalized.back();
    latest.state = latest.close_time && *latest.close_time > now ? Core::Series::BarState::Forming
                                                                 : Core::Series::BarState::Closed;
    std::vector<Core::Series::BarUpdate> updates;
    if (!previous || latest.open_time > previous->open_time) {
        normalized.back() = latest;
        updates.push_back({key, Core::Series::BarUpdateKind::Backfill, std::move(normalized)});
    } else if (latest.open_time == previous->open_time && !same_values(latest, *previous)) {
        const auto kind =
            latest.state == Core::Series::BarState::Forming
                ? Core::Series::BarUpdateKind::ReplaceForming
                : (previous->state == Core::Series::BarState::Forming ? Core::Series::BarUpdateKind::ReplaceForming
                                                                      : Core::Series::BarUpdateKind::Backfill);
        updates.push_back({key, kind, {latest}});
    }
    previous = latest;
    return updates;
}
} // namespace Didrachma::Market::Providers::Yahoo::Intern
