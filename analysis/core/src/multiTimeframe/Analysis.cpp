#include <Didrachma/analysis/core/multiTimeframe/Analysis.h>
#include <algorithm>
#include <functional>
#include <set>

namespace Didrachma::Analysis::Core::MultiTimeframe {
namespace Intern {
Market::Core::Time::UtcTimestamp bucket_open(Market::Core::Time::UtcTimestamp value, std::chrono::seconds duration) {
    const auto seconds = value.time_since_epoch().count();
    const auto aligned = seconds - ((seconds % duration.count()) + duration.count()) % duration.count();
    return Market::Core::Time::UtcTimestamp{std::chrono::seconds{aligned}};
}
} // namespace Intern

const TimedValue* align_closed(std::span<const TimedValue> values, Market::Core::Time::UtcTimestamp time) {
    const TimedValue* selected{};
    for (const auto& value : values)
        if (value.closed && value.timestamp <= time && value.available_at <= time &&
            (!selected || value.timestamp > selected->timestamp))
            selected = &value;

    return selected;
}

std::vector<GraphDiagnostic> DependencyGraph::validate() const {
    std::vector<GraphDiagnostic> diagnostics;
    for (const auto& [id, node] : m_nodes)
        for (const auto& dependency : node.dependencies)
            if (!m_nodes.contains(dependency))
                diagnostics.push_back({id, "missing input: " + dependency});

    std::map<std::string, int> state;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        if (state[id] == 1) {
            diagnostics.push_back({id, "dependency cycle"});
            return;
        }

        if (state[id] == 2)
            return;

        state[id] = 1;
        for (const auto& dependency : m_nodes.at(id).dependencies)
            if (m_nodes.contains(dependency))
                visit(dependency);
        state[id] = 2;
    };
    for (const auto& [id, node] : m_nodes)
        visit(id);

    return diagnostics;
}

std::vector<std::string> DependencyGraph::order() const {
    if (!validate().empty())
        return {};

    std::vector<std::string> result;
    std::set<std::string> visited;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        if (!visited.insert(id).second)
            return;

        for (const auto& dependency : m_nodes.at(id).dependencies)
            visit(dependency);
        result.push_back(id);
    };
    for (const auto& [id, node] : m_nodes)
        visit(id);

    return result;
}

std::map<std::string, Market::Core::Time::Range> DependencyGraph::propagate(const std::string& source,
                                                                            Market::Core::Time::Range dirty) const {
    std::map<std::string, Market::Core::Time::Range> affected;
    if (!m_nodes.contains(source) || !validate().empty())
        return affected;

    affected[source] = dirty;
    for (const auto& id : order()) {
        const auto& node = m_nodes.at(id);
        for (const auto& dependency : node.dependencies) {
            const auto found = affected.find(dependency);
            if (found == affected.end())
                continue;

            auto range = found->second;
            if (node.kind == NodeKind::Resampling && node.bucket > std::chrono::seconds::zero()) {
                range.begin = Intern::bucket_open(range.begin, node.bucket);
                const auto last = range.end > range.begin ? range.end - std::chrono::seconds{1} : range.end;
                range.end = Intern::bucket_open(last, node.bucket) + node.bucket;
            }
            range.begin -= node.lookback;
            auto [position, inserted] = affected.emplace(id, range);
            if (!inserted) {
                position->second.begin = std::min(position->second.begin, range.begin);
                position->second.end = std::max(position->second.end, range.end);
            }
        }
    }

    return affected;
}

std::vector<Condition::Event> TrendInterest::evaluate(const Market::Core::Series::Key& series,
                                                      std::span<const BoundInput> inputs,
                                                      std::span<const Market::Core::Time::UtcTimestamp> times) const {
    std::vector<Condition::Event> events;
    for (const auto time : times) {
        std::map<std::string, double> evidence;
        bool rising = true;
        for (const auto& input : inputs) {
            const auto* current = align_closed(input.values, time);
            const TimedValue* previous{};
            if (current)
                for (const auto& value : input.values)
                    if (value.closed && value.available_at <= time && value.timestamp < current->timestamp &&
                        (!previous || value.timestamp > previous->timestamp))
                        previous = &value;
            if (!current || !previous || current->value <= previous->value) {
                rising = false;
                break;
            }

            evidence[input.binding.name] = current->value;
        }
        if (!rising || evidence.size() != inputs.size())
            continue;

        const auto seconds = time.time_since_epoch().count();
        events.push_back({m_id + ":" + series.instrument + ":" + std::to_string(seconds), m_id, series, time,
                          std::nullopt, Condition::Direction::Upward, Condition::Severity::Attention,
                          "Multi-timeframe trend interest", "All configured closed timeframes are rising", evidence});
    }
    return events;
}
} // namespace Didrachma::Analysis::Core::MultiTimeframe
