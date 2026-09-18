#include <Didrachma/market/core/series/Resampler.h>
#include <algorithm>
#include <chrono>
#include <map>

namespace Didrachma::Market::Core::Series {
namespace Intern {
std::chrono::seconds duration(Time::Frame frame) {
    using enum Time::Unit;
    if (frame.unit == Minute)
        return std::chrono::minutes{frame.quantity};

    if (frame.unit == Hour)
        return std::chrono::hours{frame.quantity};

    return std::chrono::days{frame.quantity};
}
} // namespace Intern

ResampleResult resample(std::span<const Bar> input, Time::Frame source, Time::Frame target, SessionPolicy) {
    const auto source_duration = Intern::duration(source);
    const auto target_duration = Intern::duration(target);
    if (target_duration <= source_duration || target_duration % source_duration != std::chrono::seconds::zero())
        return {{}, "target timeframe must be an integral multiple of the source timeframe"};

    std::map<Time::UtcTimestamp, std::vector<Bar>> buckets;
    for (const auto& bar : input) {
        const auto seconds = bar.open_time.time_since_epoch().count();
        const auto bucket_seconds =
            seconds - ((seconds % target_duration.count()) + target_duration.count()) % target_duration.count();
        buckets[Time::UtcTimestamp{std::chrono::seconds{bucket_seconds}}].push_back(bar);
    }

    ResampleResult result;
    for (auto& [open, members] : buckets) {
        std::ranges::stable_sort(members, {}, &Bar::open_time);
        Bar aggregate = members.front();
        aggregate.open_time = open;
        aggregate.close_time = open + target_duration;
        aggregate.high = aggregate.low = aggregate.open;
        aggregate.volume = 0;
        aggregate.state = BarState::Closed;
        for (const auto& member : members) {
            aggregate.high = std::max(aggregate.high, member.high);
            aggregate.low = std::min(aggregate.low, member.low);
            aggregate.close = member.close;
            aggregate.volume += member.volume;
            if (member.state == BarState::Forming)
                aggregate.state = BarState::Forming;
        }

        const auto& last = members.back();
        if (!last.close_time || *last.close_time < *aggregate.close_time)
            aggregate.state = BarState::Forming;
        result.bars.push_back(aggregate);
    }

    return result;
}
} // namespace Didrachma::Market::Core::Series
