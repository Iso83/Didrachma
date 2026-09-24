#pragma once

#include <Didrachma/analysis/core/indicator/Analyzer.h>
#include <Didrachma/stockChart/core/Document.h>
#include <Didrachma/strategy/core/Repository.h>
#include <Didrachma/strategy/core/Runtime.h>
#include <filesystem>
#include <functional>
#include <map>

namespace Didrachma::Studio::Core {
struct StrategyDefinitionRecord {
    Strategy::Core::Definition definition;
    Strategy::Core::ExecutionCosts costs;
    std::optional<std::filesystem::path> path;
    bool unsaved{};
};

struct StrategySeriesStatus {
    std::string binding_id;
    Market::Core::Series::Key key;
    Strategy::Core::BindingState state;
    std::optional<Market::Core::Time::UtcTimestamp> last_closed;
    std::vector<std::string> condition_ids;
    std::string chart_id;
};

struct StrategyRunMonitor {
    std::string id;
    std::string definition_id;
    std::optional<std::string> subject;
    Strategy::Core::Snapshot snapshot;
    Strategy::Core::RunState state{Strategy::Core::RunState::WaitingForEntry};
    Strategy::Core::RunResult result;
    std::vector<StrategySeriesStatus> series;
    std::vector<Strategy::Core::Evidence> evidence;
    bool restored_summary{};
};

class Strategies {
    struct Runtime;
    struct OwnedIndicator {
        std::string chart_id;
        std::string instance_id;
        std::size_t references{};
    };

    std::vector<StrategyDefinitionRecord> m_definitions;
    std::vector<StrategyRunMonitor> m_runs;
    std::map<std::string, OwnedIndicator> m_owned_indicators;
    std::map<std::string, std::shared_ptr<Runtime>> m_runtime;
    Analysis::Core::Indicator::Analyzer* m_analyzer;
    std::uint64_t m_next_id{1};

public:
    explicit Strategies(Analysis::Core::Indicator::Analyzer& analyzer) : m_analyzer(&analyzer) {}
    ~Strategies();
    Strategies(const Strategies&) = delete;
    Strategies& operator=(const Strategies&) = delete;

    [[nodiscard]] std::span<StrategyDefinitionRecord> definitions() {
        return m_definitions;
    }
    [[nodiscard]] std::span<const StrategyDefinitionRecord> definitions() const {
        return m_definitions;
    }
    [[nodiscard]] std::span<const StrategyRunMonitor> runs() const {
        return m_runs;
    }

    StrategyDefinitionRecord& create(Strategy::Core::Definition definition);
    StrategyDefinitionRecord& duplicate(std::string_view definition_id);
    bool erase(std::string_view definition_id, bool discard_unsaved = false);
    [[nodiscard]] std::vector<Strategy::Core::RepositoryError>
    save(std::string_view definition_id, const std::filesystem::path&,
         std::span<const Analysis::Core::Indicator::Definition> catalog);
    [[nodiscard]] std::vector<Strategy::Core::RepositoryError>
    import(const std::filesystem::path&, std::span<const Analysis::Core::Indicator::Definition> catalog);

    StrategyRunMonitor* start(std::string_view definition_id, std::optional<std::string> subject,
                              std::span<const Analysis::Core::Indicator::Definition> catalog,
                              std::function<StockChart::Core::Document&(const Market::Core::Series::Key&)> chart);
    bool stop(std::string_view run_id, Market::Core::Time::UtcTimestamp time,
              std::function<StockChart::Core::Document*(std::string_view)> chart);
    void update(std::function<std::span<const Market::Core::Series::Bar>(std::string_view chart_id)> bars,
                std::function<StockChart::Core::Document*(std::string_view)> chart = {});
    [[nodiscard]] bool close_view(std::string_view chart_id) const;
    [[nodiscard]] bool chart_required(std::string_view chart_id) const;

    [[nodiscard]] std::optional<std::string> save_session(const std::filesystem::path&) const;
    [[nodiscard]] std::optional<std::string>
    restore_session(const std::filesystem::path&, std::span<const Analysis::Core::Indicator::Definition> catalog);

private:
    StrategyDefinitionRecord* find(std::string_view id);
    void release_owned(StrategyRunMonitor&, std::function<StockChart::Core::Document*(std::string_view)>);
};
} // namespace Didrachma::Studio::Core
