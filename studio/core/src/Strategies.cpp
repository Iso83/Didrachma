#include <Didrachma/stockChart/core/PatternProjection.h>
#include <Didrachma/strategy/core/Validation.h>
#include <Didrachma/studio/core/Strategies.h>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

namespace Didrachma::Studio::Core {
struct Strategies::Runtime {
    std::unique_ptr<Strategy::Core::DataGraph> graph;
    std::unique_ptr<Strategy::Core::Engine> engine;
    std::map<std::string, std::vector<std::uint64_t>> bar_signatures;
    std::uint64_t revision{};
};

Strategies::~Strategies() = default;
namespace Intern {
std::uint64_t bar_signature(const Market::Core::Series::Bar& bar) {
    auto result = static_cast<std::uint64_t>(bar.open_time.time_since_epoch().count());
    const auto combine = [&](auto value) {
        result ^= std::hash<decltype(value)>{}(value) + 0x9e3779b97f4a7c15ULL + (result << 6U) + (result >> 2U);
    };
    combine(bar.close_time ? bar.close_time->time_since_epoch().count() : std::int64_t{});
    combine(bar.open);
    combine(bar.high);
    combine(bar.low);
    combine(bar.close);
    combine(bar.volume);
    combine(static_cast<int>(bar.state));
    return result;
}

void collect_conditions(const std::shared_ptr<Strategy::Core::ConditionExpression>& expression,
                        std::map<std::string, std::vector<std::string>>& indicator_conditions,
                        std::map<std::string, std::vector<std::string>>& series_conditions,
                        const Strategy::Core::Definition& definition) {
    if (!expression)
        return;

    const auto add_indicator = [&](std::string_view id) {
        indicator_conditions[std::string{id}].push_back(expression->id);
    };
    if (const auto* value = std::get_if<Strategy::Core::MarketComparison>(&expression->predicate))
        series_conditions[value->series_id].push_back(expression->id);
    else if (const auto* value = std::get_if<Strategy::Core::IndicatorComparison>(&expression->predicate))
        add_indicator(value->indicator_id);
    else if (const auto* value = std::get_if<Strategy::Core::IndicatorCross>(&expression->predicate)) {
        add_indicator(value->left_indicator_id);
        if (value->right_indicator_id)
            add_indicator(*value->right_indicator_id);
    } else if (const auto* value = std::get_if<Strategy::Core::PatternOccurrence>(&expression->predicate))
        add_indicator(value->indicator_id);
    for (const auto& child : expression->children)
        collect_conditions(child, indicator_conditions, series_conditions, definition);
}
} // namespace Intern

StrategyDefinitionRecord& Strategies::create(Strategy::Core::Definition definition) {
    m_definitions.push_back({std::move(definition), {}, std::nullopt, true});
    return m_definitions.back();
}

StrategyDefinitionRecord& Strategies::duplicate(std::string_view definition_id) {
    const auto* original = find(definition_id);
    if (!original)
        throw std::invalid_argument("Unknown strategy definition");

    auto copy = original->definition;
    const auto costs = original->costs;
    copy.id += "-copy-" + std::to_string(m_next_id++);
    copy.display_name += " copy";
    auto& result = create(std::move(copy));
    result.costs = costs;
    return result;
}

bool Strategies::erase(std::string_view definition_id, bool discard_unsaved) {
    const auto found =
        std::ranges::find(m_definitions, definition_id, [](const auto& item) { return item.definition.id; });
    if (found == m_definitions.end() || (found->unsaved && !discard_unsaved) ||
        std::ranges::any_of(m_runs, [&](const auto& run) {
            return run.definition_id == definition_id && run.state != Strategy::Core::RunState::Stopped;
        }))
        return false;

    m_definitions.erase(found);
    return true;
}

std::vector<Strategy::Core::RepositoryError>
Strategies::save(std::string_view definition_id, const std::filesystem::path& path,
                 std::span<const Analysis::Core::Indicator::Definition> catalog) {
    auto* record = find(definition_id);
    if (!record)
        return {{"definition", "Unknown strategy definition"}};

    Strategy::Core::JsonFileRepository repository(path, {catalog.begin(), catalog.end()});
    auto errors = repository.save(record->definition);
    if (errors.empty()) {
        record->path = path;
        record->unsaved = false;
    }
    return errors;
}

std::vector<Strategy::Core::RepositoryError>
Strategies::import(const std::filesystem::path& path, std::span<const Analysis::Core::Indicator::Definition> catalog) {
    Strategy::Core::JsonFileRepository repository(path, {catalog.begin(), catalog.end()});
    auto loaded = repository.load();
    if (auto* errors = std::get_if<std::vector<Strategy::Core::RepositoryError>>(&loaded))
        return *errors;

    auto definition = std::get<Strategy::Core::Definition>(std::move(loaded));
    if (auto* existing = find(definition.id)) {
        existing->definition = std::move(definition);
        existing->path = path;
        existing->unsaved = false;
    } else
        m_definitions.push_back({std::move(definition), {}, path, false});
    return {};
}

StrategyRunMonitor*
Strategies::start(std::string_view definition_id, std::optional<std::string> subject,
                  std::span<const Analysis::Core::Indicator::Definition> catalog,
                  std::function<StockChart::Core::Document&(const Market::Core::Series::Key&)> chart) {
    auto* record = find(definition_id);
    if (!record || !Strategy::Core::validate(record->definition, catalog).empty())
        return nullptr;
    auto resolution = Strategy::Core::resolve(record->definition, subject);
    if (!resolution.errors.empty())
        return nullptr;

    std::map<std::string, std::vector<std::string>> indicator_conditions, series_conditions;
    Intern::collect_conditions(record->definition.entry.condition, indicator_conditions, series_conditions,
                               record->definition);
    for (const auto& exit : record->definition.exits)
        Intern::collect_conditions(exit.condition, indicator_conditions, series_conditions, record->definition);
    for (const auto& rule : record->definition.runtime_rules)
        Intern::collect_conditions(rule.condition, indicator_conditions, series_conditions, record->definition);

    StrategyRunMonitor monitor{"run-" + std::to_string(m_next_id++), record->definition.id, subject,
                               Strategy::Core::Snapshot{record->definition}};
    for (const auto& resolved : resolution.series) {
        auto& document = chart(resolved.key);
        monitor.series.push_back({resolved.binding_id,
                                  resolved.key,
                                  {},
                                  std::nullopt,
                                  series_conditions[resolved.binding_id],
                                  document.id()});
        for (const auto& indicator : record->definition.indicators) {
            if (indicator.series_id != resolved.binding_id)
                continue;

            const auto ownership_id = record->definition.id + "/" + indicator.id + "/" + document.id();
            auto& owned = m_owned_indicators[ownership_id];
            if (owned.references == 0) {
                owned.chart_id = document.id();
                owned.instance_id = document.add_indicator(indicator.definition_id, indicator.parameters);
                document.set_indicator_name(owned.instance_id, "Strategy: " + indicator.id);
                const auto definition =
                    std::ranges::find(catalog, indicator.definition_id, &Analysis::Core::Indicator::Definition::id);
                if (definition != catalog.end()) {
                    if (definition->capability == Analysis::Core::Indicator::Capability::AnalysisEvent)
                        StockChart::Core::reconcile_pattern_projection(document, *definition, owned.instance_id);
                    else
                        for (const auto& output : definition->outputs)
                            if (output.visual == Analysis::Core::Indicator::VisualKind::Line)
                                document.add_layer(StockChart::Core::LayerKind::Line, {{owned.instance_id, output.id}});
                            else if (output.visual == Analysis::Core::Indicator::VisualKind::Histogram)
                                document.add_layer(StockChart::Core::LayerKind::Histogram,
                                                   {{owned.instance_id, output.id}});
                }
            }
            ++owned.references;
            for (const auto& condition : indicator_conditions[indicator.id])
                monitor.series.back().condition_ids.push_back(condition);
        }
    }
    m_runs.push_back(std::move(monitor));
    auto runtime = std::make_shared<Runtime>();
    runtime->graph =
        std::make_unique<Strategy::Core::DataGraph>(m_runs.back().snapshot, *m_analyzer, m_runs.back().subject);
    runtime->engine = std::make_unique<Strategy::Core::Engine>(m_runs.back().snapshot, *runtime->graph, record->costs);
    m_runtime.emplace(m_runs.back().id, std::move(runtime));
    return &m_runs.back();
}

bool Strategies::stop(std::string_view run_id, Market::Core::Time::UtcTimestamp time,
                      std::function<StockChart::Core::Document*(std::string_view)> chart) {
    auto found = std::ranges::find(m_runs, run_id, &StrategyRunMonitor::id);
    if (found == m_runs.end() || found->state == Strategy::Core::RunState::Stopped)
        return false;

    found->state = Strategy::Core::RunState::Stopped;
    found->result.state = Strategy::Core::RunState::Stopped;
    found->result.exit_reason = Strategy::Core::ExitReason::UserStop;
    release_owned(*found, std::move(chart));
    found->result.events.push_back({Strategy::Core::StrategyEventKind::RunStopped,
                                    time,
                                    time,
                                    Strategy::Core::RunState::WaitingForEntry,
                                    Strategy::Core::RunState::Stopped,
                                    {},
                                    {},
                                    {},
                                    {},
                                    "user stop"});
    m_runtime.erase(found->id);
    return true;
}

void Strategies::release_owned(StrategyRunMonitor& run,
                               std::function<StockChart::Core::Document*(std::string_view)> chart) {
    for (const auto& indicator : run.snapshot.definition().indicators) {
        const auto series = std::ranges::find(run.series, indicator.series_id, &StrategySeriesStatus::binding_id);
        if (series == run.series.end())
            continue;

        const auto ownership_id = run.definition_id + "/" + indicator.id + "/" + series->chart_id;
        auto owned = m_owned_indicators.find(ownership_id);
        if (owned == m_owned_indicators.end() || --owned->second.references != 0)
            continue;

        if (chart)
            if (auto* document = chart(owned->second.chart_id))
                document->remove_indicator(owned->second.instance_id);
        m_owned_indicators.erase(owned);
    }
}

void Strategies::update(std::function<std::span<const Market::Core::Series::Bar>(std::string_view)> bars,
                        std::function<StockChart::Core::Document*(std::string_view)> chart) {
    for (auto& monitor : m_runs) {
        auto runtime = m_runtime.find(monitor.id);
        if (runtime == m_runtime.end() || monitor.state == Strategy::Core::RunState::Stopped)
            continue;

        bool changed = false;
        bool correction = false;
        std::optional<Market::Core::Series::Bar> appended_primary;
        for (auto& series : monitor.series) {
            const auto values = bars(series.chart_id);
            std::vector<std::uint64_t> signatures;
            for (const auto& bar : values)
                if (bar.state == Market::Core::Series::BarState::Closed)
                    signatures.push_back(Intern::bar_signature(bar));
            auto& previous = runtime->second->bar_signatures[series.binding_id];
            if (previous == signatures)
                continue;

            const bool append = signatures.size() == previous.size() + 1 &&
                                std::equal(previous.begin(), previous.end(), signatures.begin());
            correction = correction || (!previous.empty() && !append);
            if (append && series.binding_id == monitor.snapshot.definition().primary_series_id) {
                const auto last = std::find_if(values.rbegin(), values.rend(), [](const auto& bar) {
                    return bar.state == Market::Core::Series::BarState::Closed;
                });
                if (last != values.rend())
                    appended_primary = *last;
            }
            previous = std::move(signatures);
            runtime->second->graph->set_series(series.binding_id, values, ++runtime->second->revision);
            series.state = runtime->second->graph->state(series.binding_id);
            const auto last = std::find_if(values.rbegin(), values.rend(), [](const auto& bar) {
                return bar.state == Market::Core::Series::BarState::Closed;
            });
            series.last_closed = last == values.rend() ? std::nullopt : std::optional{last->open_time};
            changed = true;
        }
        if (!changed)
            continue;

        const auto* definition = find(monitor.definition_id);
        if (correction || !appended_primary || monitor.result.closed_bar_count == 0) {
            runtime->second->engine = std::make_unique<Strategy::Core::Engine>(
                monitor.snapshot, *runtime->second->graph,
                definition ? definition->costs : Strategy::Core::ExecutionCosts{});
            monitor.result = runtime->second->engine->replay();
        } else {
            runtime->second->engine->process(*appended_primary);
            monitor.result = runtime->second->engine->result();
        }
        monitor.state = monitor.result.state;
        monitor.evidence.clear();
        const auto evaluations = runtime->second->graph->evaluate_entry();
        if (!evaluations.empty())
            monitor.evidence = evaluations.back().evidence;
        if (monitor.state == Strategy::Core::RunState::Exited || monitor.state == Strategy::Core::RunState::Stopped ||
            monitor.state == Strategy::Core::RunState::Error) {
            release_owned(monitor, chart);
            m_runtime.erase(monitor.id);
        }
    }
}

std::optional<std::string> Strategies::save_session(const std::filesystem::path& path) const {
    nlohmann::json root{{"version", 1}, {"definitions", nlohmann::json::array()}, {"runs", nlohmann::json::array()}};
    for (const auto& record : m_definitions)
        if (record.path)
            root["definitions"].push_back({{"path", record.path->string()},
                                           {"quantity", record.costs.quantity},
                                           {"startingCapital", record.costs.starting_capital},
                                           {"fixedPerFill", record.costs.fixed_per_fill},
                                           {"percentagePerFill", record.costs.percentage_per_fill},
                                           {"slippagePercentage", record.costs.slippage_percentage}});
    for (const auto& run : m_runs)
        root["runs"].push_back({{"id", run.id},
                                {"definitionId", run.definition_id},
                                {"subject", run.subject},
                                {"state", static_cast<int>(run.state)},
                                {"exitReason", static_cast<int>(run.result.exit_reason)},
                                {"netProfitLoss", run.result.net_profit_loss},
                                {"returnPercentage", run.result.return_percentage}});
    std::ofstream output(path, std::ios::trunc);
    if (!output || !(output << root.dump(2)))
        return "Unable to save strategy session";

    return std::nullopt;
}

std::optional<std::string> Strategies::restore_session(const std::filesystem::path& path,
                                                       std::span<const Analysis::Core::Indicator::Definition> catalog) {
    try {
        std::ifstream input(path);
        if (!input)
            return "Unable to open strategy session";

        nlohmann::json root;
        input >> root;
        if (root.at("version").get<int>() != 1)
            return "Unsupported strategy session version";

        for (const auto& definition : root.at("definitions")) {
            const auto errors = import(definition.at("path").get<std::string>(), catalog);
            if (!errors.empty())
                return errors.front().message;

            auto* record = find(m_definitions.back().definition.id);
            record->costs.quantity = definition.value("quantity", 1.0);
            if (!definition.at("startingCapital").is_null())
                record->costs.starting_capital = definition.at("startingCapital").get<double>();
            record->costs.fixed_per_fill = definition.value("fixedPerFill", 0.0);
            record->costs.percentage_per_fill = definition.value("percentagePerFill", 0.0);
            record->costs.slippage_percentage = definition.value("slippagePercentage", 0.0);
        }
        for (const auto& encoded : root.at("runs")) {
            auto* definition = find(encoded.at("definitionId").get<std::string>());
            if (!definition)
                continue;

            const auto subject = encoded.at("subject").is_null()
                                     ? std::optional<std::string>{}
                                     : std::optional{encoded.at("subject").get<std::string>()};
            StrategyRunMonitor summary{encoded.at("id").get<std::string>(), definition->definition.id, subject,
                                       Strategy::Core::Snapshot{definition->definition}};
            summary.state = static_cast<Strategy::Core::RunState>(encoded.at("state").get<int>());
            summary.result.state = summary.state;
            summary.result.exit_reason = static_cast<Strategy::Core::ExitReason>(encoded.at("exitReason").get<int>());
            summary.result.net_profit_loss = encoded.at("netProfitLoss").get<double>();
            summary.result.return_percentage = encoded.at("returnPercentage").get<double>();
            summary.restored_summary = true;
            m_runs.push_back(std::move(summary));
        }
        return std::nullopt;
    } catch (const std::exception& error) {
        return error.what();
    }
}

bool Strategies::close_view(std::string_view chart_id) const {
    return !chart_required(chart_id);
}

bool Strategies::chart_required(std::string_view chart_id) const {
    return std::ranges::any_of(m_runs, [&](const auto& run) {
        return run.state != Strategy::Core::RunState::Stopped &&
               std::ranges::any_of(run.series, [&](const auto& series) { return series.chart_id == chart_id; });
    });
}

StrategyDefinitionRecord* Strategies::find(std::string_view id) {
    const auto found = std::ranges::find(m_definitions, id, [](const auto& item) { return item.definition.id; });
    return found == m_definitions.end() ? nullptr : &*found;
}
} // namespace Didrachma::Studio::Core
