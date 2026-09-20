#include "Application.h"

#include "FixtureProvider.h"
#include "Report.h"
#include "UtcTime.h"

#include <CLI/CLI.hpp>
#include <Didrachma/analysis/adapters/talib/Analyzer.h>
#include <Didrachma/market/providers/yahoo/Provider.h>
#include <Didrachma/strategy/core/Repository.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

namespace Didrachma::Apps::Strategy::Intern {
using namespace Didrachma::Strategy::Core;

struct Options {
    std::string strategy;
    std::string subject;
    std::string from;
    std::string through;
    std::string provider{"fixture"};
    std::string provider_config;
    std::string output;
    double quantity{1};
    std::optional<double> starting_capital;
    double fixed_cost{};
    double percentage_cost{};
    double slippage{};
    bool verbose{};
    bool costs_overridden{};
};

struct LoadGroup {
    Market::Core::Series::Key key;
    std::vector<std::string> binding_ids;
    std::size_t required{};
};

std::string key_text(const Market::Core::Series::Key& key) {
    return key.provider + '\n' + key.instrument + '\n' + std::to_string(key.timeframe.quantity) + '\n' +
           std::to_string(static_cast<int>(key.timeframe.unit));
}

std::chrono::seconds frame_duration(Market::Core::Time::Frame frame) {
    using enum Market::Core::Time::Unit;
    const auto unit = frame.unit == Minute ? std::chrono::minutes{1}
                      : frame.unit == Hour ? std::chrono::hours{1}
                                           : std::chrono::hours{24};
    return std::chrono::duration_cast<std::chrono::seconds>(unit) * frame.quantity;
}

std::vector<LoadGroup> make_load_plan(const DataGraph& graph) {
    std::map<std::string, LoadGroup> grouped;
    for (const auto& resolved : graph.resolution().series) {
        auto& group = grouped[key_text(resolved.key)];
        group.key = resolved.key;
        group.binding_ids.push_back(resolved.binding_id);
        group.required =
            std::max(group.required, std::max<std::size_t>(graph.required_history(resolved.binding_id), 1));
    }
    std::vector<LoadGroup> result;
    for (auto& [key, group] : grouped)
        result.push_back(std::move(group));

    return result;
}

bool load_group(Market::Core::Provider::Data& provider, DataGraph& graph, const LoadGroup& group,
                Market::Core::Time::UtcTimestamp from, Market::Core::Time::UtcTimestamp through, std::ostream& error) {
    constexpr int maximum_attempts = 5;
    const auto interval = frame_duration(group.key.timeframe);
    auto lookback = interval * static_cast<std::int64_t>(std::max<std::size_t>(group.required * 2, 4));
    for (int attempt = 1; attempt <= maximum_attempts; ++attempt) {
        const auto loaded = provider.load_history({group.key, {from - lookback, through + std::chrono::seconds{1}}});
        if (const auto* failure = std::get_if<Market::Core::Provider::Error>(&loaded)) {
            error << group.binding_ids.front() << ": provider error: " << failure->message << '\n';
            return false;
        }

        const auto& bars = std::get<std::vector<Market::Core::Series::Bar>>(loaded);
        for (const auto& binding : group.binding_ids)
            graph.set_series(binding, bars, static_cast<std::uint64_t>(attempt));
        if (bars.size() >= group.required)
            return true;

        lookback *= 2;
    }
    error << group.binding_ids.front() << ": insufficient history after bounded warm-up retries; required "
          << group.required << " bars\n";
    return false;
}

bool needs_subject(const Definition& definition) {
    return std::ranges::any_of(definition.series,
                               [](const auto& binding) { return binding.instrument.kind == InstrumentKind::Subject; });
}

std::unique_ptr<Market::Core::Provider::Data> default_provider(std::string_view id,
                                                               const std::filesystem::path& configuration) {
    if (id == "fixture") {
        if (configuration.empty())
            throw std::runtime_error("--provider-config is required for the fixture provider");

        return std::make_unique<FixtureProvider>(configuration);
    }
    if (id == "yahoo")
        return std::make_unique<Market::Providers::Yahoo::Provider>();

    throw std::runtime_error("Unsupported provider: " + std::string{id});
}

int execute(const Options& options, const ProviderFactory& provider_factory, std::ostream& output,
            std::ostream& error) {
    const auto from = parse_utc(options.from);
    const auto through = parse_utc(options.through);
    if (!from || !through)
        throw std::runtime_error("--from and --through must use the exact UTC form YYYY-MM-DDTHH:MM:SSZ");

    if (*from > *through)
        throw std::runtime_error("--from must not exceed --through");

    Analysis::Adapters::TaLib::Analyzer analyzer;
    JsonFileRepository repository{options.strategy, analyzer.catalog()};
    const auto loaded = repository.load();
    if (const auto* errors = std::get_if<std::vector<RepositoryError>>(&loaded)) {
        for (const auto& item : *errors)
            error << item.path << ": " << item.message << '\n';

        return 2;
    }

    const auto& definition = std::get<Definition>(loaded);
    if (needs_subject(definition) && options.subject.empty())
        throw std::runtime_error("--subject is required because the strategy contains Subject bindings");

    for (const auto& binding : definition.series)
        if (binding.provider_id != options.provider)
            throw std::runtime_error("Binding '" + binding.id + "' requires provider '" + binding.provider_id +
                                     "', not selected provider '" + options.provider + "'");

    const std::optional<std::string> subject = options.subject.empty() ? std::nullopt : std::optional{options.subject};
    DataGraph graph{Snapshot{definition}, analyzer, subject};
    if (!graph.resolution().errors.empty()) {
        for (const auto& item : graph.resolution().errors)
            error << item.binding_id << ": " << item.message << '\n';

        return 2;
    }

    auto provider = provider_factory(options.provider, options.provider_config);
    for (const auto& group : make_load_plan(graph))
        if (!load_group(*provider, graph, group, *from, *through, error))
            return 3;

    (void)graph.evaluate_entry();
    for (const auto& resolved : graph.resolution().series) {
        const auto state = graph.state(resolved.binding_id);
        if (state.readiness != Readiness::Ready) {
            error << resolved.binding_id << ": "
                  << (state.detail.empty() ? "required data is unavailable" : state.detail) << '\n';
            return 3;
        }
    }

    const ExecutionCosts costs{options.quantity, options.starting_capital, options.fixed_cost, options.percentage_cost,
                               options.slippage};
    Engine engine{Snapshot{definition}, graph, costs};
    for (const auto& bar : graph.primary_bars())
        if (bar.close_time && *bar.close_time >= *from && *bar.close_time <= *through)
            engine.process(bar);
    const auto result = engine.finish();
    const ReportContext context{options.strategy,        options.provider, subject, *from, *through, costs,
                                options.costs_overridden};
    const auto report = make_report(definition, graph, result, context);
    print_summary(report, options.verbose, output);
    if (!options.output.empty()) {
        std::ofstream file{options.output};
        if (!file)
            throw std::runtime_error("Cannot open report output: " + options.output);

        file << report.dump(2) << '\n';
    }

    return result.state == RunState::Error ? 4 : 0;
}
} // namespace Didrachma::Apps::Strategy::Intern

namespace Didrachma::Apps::Strategy {
Application::Application(ProviderFactory provider_factory)
    : m_provider_factory(provider_factory ? std::move(provider_factory) : Intern::default_provider) {}

int Application::run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error) const {
    Intern::Options options;
    CLI::App app{"Deterministic Didrachma strategy runner"};
    app.add_option("--strategy", options.strategy, "Versioned strategy JSON file")
        ->required()
        ->check(CLI::ExistingFile);
    app.add_option("--subject", options.subject, "Symbol substituted for Subject bindings");
    app.add_option("--from", options.from, "First included primary-bar close (UTC)")->required();
    app.add_option("--through", options.through, "Last included primary-bar close (UTC)")->required();
    app.add_option("--provider", options.provider, "Provider adapter: fixture or yahoo")
        ->check(CLI::IsMember({"fixture", "yahoo"}));
    app.add_option("--provider-config", options.provider_config, "Fixture provider JSON file");
    app.add_option("--output", options.output, "Write the versioned JSON report to this file");
    auto* quantity = app.add_option("--quantity", options.quantity, "Execution quantity")->check(CLI::PositiveNumber);
    auto* capital = app.add_option("--starting-capital", options.starting_capital, "Starting capital override")
                        ->check(CLI::PositiveNumber);
    auto* fixed = app.add_option("--fixed-cost", options.fixed_cost, "Fixed transaction cost per fill")
                      ->check(CLI::NonNegativeNumber);
    auto* percentage = app.add_option("--percentage-cost", options.percentage_cost, "Percentage cost per fill")
                           ->check(CLI::NonNegativeNumber);
    auto* slippage =
        app.add_option("--slippage", options.slippage, "Slippage percentage")->check(CLI::NonNegativeNumber);
    app.add_flag("--verbose", options.verbose, "Print the complete strategy event trace");

    std::vector<std::string> storage{"Didrachma_apps_strategy"};
    for (const auto argument : arguments)
        storage.emplace_back(argument);
    std::vector<char*> argv;
    for (auto& argument : storage)
        argv.push_back(argument.data());
    try {
        app.parse(static_cast<int>(argv.size()), argv.data());
        options.costs_overridden =
            quantity->count() || capital->count() || fixed->count() || percentage->count() || slippage->count();
        return Intern::execute(options, m_provider_factory, output, error);
    } catch (const CLI::ParseError& parse_error) {
        return app.exit(parse_error, output, error);
    } catch (const std::exception& exception) {
        error << "strategy run failed: " << exception.what() << '\n';
        return 2;
    }
}
} // namespace Didrachma::Apps::Strategy
