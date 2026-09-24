#pragma once

#include <Didrachma/analysis/core/condition/Event.h>
#include <Didrachma/market/core/series/Bar.h>
#include <Didrachma/market/core/time/Frame.h>
#include <Didrachma/market/core/time/Range.h>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Didrachma::Analysis::Core::MultiTimeframe {
enum class InputKind { Series, Indicator };

struct InputBinding {
    std::string name;
    InputKind kind{InputKind::Series};
    Market::Core::Time::Frame timeframe;
    std::string source;
};

// `available_at` is the close/publication time. Alignment never uses values from the future.
struct TimedValue {
    Market::Core::Time::UtcTimestamp timestamp;
    Market::Core::Time::UtcTimestamp available_at;
    double value{};
    bool closed{true};
};

[[nodiscard]] const TimedValue* align_closed(std::span<const TimedValue> values,
                                             Market::Core::Time::UtcTimestamp evaluation_time);

enum class NodeKind { Series, Resampling, Indicator, PatternEvent, Condition };

struct Node {
    std::string id;
    NodeKind kind{NodeKind::Series};
    std::vector<std::string> dependencies;
    std::chrono::seconds lookback{};
};

struct GraphDiagnostic {
    std::string node;
    std::string message;
};

class DependencyGraph {
    std::map<std::string, Node> m_nodes;

public:
    bool add(Node node) {
        return m_nodes.emplace(node.id, std::move(node)).second;
    }

    [[nodiscard]] std::vector<GraphDiagnostic> validate() const;
    [[nodiscard]] std::vector<std::string> order() const;
    [[nodiscard]] std::map<std::string, Market::Core::Time::Range> propagate(const std::string& source,
                                                                             Market::Core::Time::Range dirty) const;
};

struct BoundInput {
    InputBinding binding;
    std::span<const TimedValue> values;
};

class TrendInterest {
    std::string m_id{"multi-timeframe-trend-interest"};

public:
    [[nodiscard]] const std::string& id() const {
        return m_id;
    }
    [[nodiscard]] std::vector<Condition::Event> evaluate(const Market::Core::Series::Key& series,
                                                         std::span<const BoundInput> inputs,
                                                         std::span<const Market::Core::Time::UtcTimestamp> times) const;
};

// Future scripted factories can describe bindings without depending on an editor or rendering API.
struct DefinitionSchema {
    std::string id;
    std::vector<InputBinding> inputs;
};
} // namespace Didrachma::Analysis::Core::MultiTimeframe
