#include "Application.h"

#include "FixtureProvider.h"
#include "Report.h"
#include "UtcTime.h"

#include <CLI/CLI.hpp>
#include <Didrachma/analysis/adapters/talib/Analyzer.h>
#include <Didrachma/market/providers/yahoo/Interval.h>
#include <Didrachma/market/providers/yahoo/Provider.h>
#include <Didrachma/strategy/core/Backtest.h>
#include <Didrachma/strategy/core/Repository.h>
#include <algorithm>
#include <fstream>
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

bool needs_subject(const Definition& definition) {
    return std::ranges::any_of(definition.series,
                               [](const auto& binding) { return binding.instrument.kind == InstrumentKind::Subject; });
}

std::vector<Market::Core::Time::Frame> supported_timeframes(std::string_view provider, const Definition& definition) {
    std::vector<Market::Core::Time::Frame> result;
    if (provider == "yahoo") {
        for (const auto& interval : Market::Providers::Yahoo::intervals())
            result.push_back(interval.frame);
    } else {
        for (const auto& binding : definition.series)
            if (std::ranges::find(result, binding.timeframe) == result.end())
                result.push_back(binding.timeframe);
    }
    return result;
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

int execute(const Options& options, const ProviderFactory& provider_factory, const RunObserver& run_observer,
            std::ostream& output, std::ostream& error) {
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

    const Definition* definition_pointer{};
    if (const auto* migrated = std::get_if<MigratedDefinition>(&loaded)) {
        definition_pointer = &migrated->definition;
        for (const auto& warning : migrated->warnings)
            error << "warning " << warning.path << ": " << warning.message << '\n';
    } else
        definition_pointer = &std::get<Definition>(loaded);
    const auto& definition = *definition_pointer;
    if (needs_subject(definition) && options.subject.empty())
        throw std::runtime_error("--subject is required because the strategy contains Subject bindings");

    for (const auto& binding : definition.series)
        if (binding.provider_id != options.provider)
            throw std::runtime_error("Binding '" + binding.id + "' requires provider '" + binding.provider_id +
                                     "', not selected provider '" + options.provider + "'");

    const std::optional<std::string> subject = options.subject.empty() ? std::nullopt : std::optional{options.subject};
    auto provider = provider_factory(options.provider, options.provider_config);
    const ExecutionCosts costs{options.quantity, options.starting_capital, options.fixed_cost, options.percentage_cost,
                               options.slippage};
    BacktestRequest request{definition, subject, *from, *through, {options.provider, {}}, costs, 1};
    if (!options.provider_config.empty())
        request.provider.values["configuration"] = options.provider_config;
    const auto frames = supported_timeframes(options.provider, definition);
    const auto outcome = BacktestRunner{analyzer}.run(request, *provider, frames);
    if (run_observer)
        run_observer(request, outcome);
    if (outcome.status == BacktestStatus::Failed || outcome.status == BacktestStatus::Cancelled) {
        for (const auto& item : outcome.errors)
            error << item.path << ": " << item.message << '\n';
        const auto data_error = std::ranges::any_of(outcome.errors, [](const auto& item) {
            return item.code == BacktestErrorCode::ProviderFailure ||
                   item.code == BacktestErrorCode::InsufficientWarmup ||
                   item.code == BacktestErrorCode::NoBarsInRange ||
                   item.code == BacktestErrorCode::UnsupportedTimeframe;
        });
        return data_error ? 3 : 4;
    }
    const ReportContext context{options.strategy,        options.provider, subject, *from, *through, costs,
                                options.costs_overridden};
    const auto report = make_report(outcome, context);
    print_summary(report, options.verbose, output);
    if (!options.output.empty()) {
        std::ofstream file{options.output};
        if (!file)
            throw std::runtime_error("Cannot open report output: " + options.output);

        file << report.dump(2) << '\n';
    }

    return 0;
}
} // namespace Didrachma::Apps::Strategy::Intern

namespace Didrachma::Apps::Strategy {
Application::Application(ProviderFactory provider_factory, RunObserver run_observer)
    : m_provider_factory(provider_factory ? std::move(provider_factory) : Intern::default_provider),
      m_run_observer(std::move(run_observer)) {}

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
        return Intern::execute(options, m_provider_factory, m_run_observer, output, error);
    } catch (const CLI::ParseError& parse_error) {
        return app.exit(parse_error, output, error);
    } catch (const std::exception& exception) {
        error << "strategy run failed: " << exception.what() << '\n';
        return 2;
    }
}
} // namespace Didrachma::Apps::Strategy
