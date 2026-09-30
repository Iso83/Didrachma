#include "Application.h"
#include "FixtureProvider.h"
#include "Report.h"
#include "TestAssert.h"
#include "UtcTime.h"

#include <Didrachma/analysis/adapters/talib/Analyzer.h>
#include <Didrachma/studio/core/HistoricalBacktests.h>
#include <Didrachma/studio/core/HistoricalResults.h>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

using namespace Didrachma;
using namespace Didrachma::Apps::Strategy;

namespace {
struct TemporaryDirectory {
    std::filesystem::path path{std::filesystem::temp_directory_path() /
                               ("didrachma-historical-backtest-" +
                                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    TemporaryDirectory() {
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::filesystem::remove_all(path);
    }
};

std::filesystem::path fixtures() {
    return std::filesystem::path{DIDRACHMA_HISTORICAL_BACKTEST_FIXTURE_PATH};
}

nlohmann::json canonical(const Strategy::Core::BacktestOutcome& outcome) {
    auto value = make_report(outcome, {"presentation-only", false});
    value["strategy"].erase("sourceFile");
    value.erase("timings");
    return value;
}

struct Scenario {
    const char* name;
    Strategy::Core::BacktestResultStatus status;
    Strategy::Core::EntryStatus entry;
    Strategy::Core::ExitReason exit;
};

int assert_trade_contract(const Scenario& scenario, const Strategy::Core::BacktestOutcome& outcome) {
    CPPTEST_ASSERT(outcome.status == Strategy::Core::BacktestStatus::Completed);
    if (scenario.status == Strategy::Core::BacktestResultStatus::NoSignal) {
        CPPTEST_ASSERT(outcome.occurrences.empty() && outcome.summary.signal_count == 0 &&
                       outcome.summary.filled_count == 0 && outcome.summary.closed_count == 0 &&
                       outcome.summary.gross_profit_loss == 0 && outcome.summary.costs == 0 &&
                       outcome.summary.net_profit_loss == 0 && !outcome.evaluations.empty() &&
                       !outcome.evaluations.back().occurrence_id &&
                       outcome.evaluations.back().evaluation.time == outcome.request.through);
        return 0;
    }
    CPPTEST_ASSERT(!outcome.occurrences.empty());
    CPPTEST_ASSERT(outcome.result_status == scenario.status &&
                   outcome.occurrences.front().result.entry_status == scenario.entry &&
                   outcome.occurrences.front().result.exit_reason == scenario.exit);
    const bool entered = scenario.entry == Strategy::Core::EntryStatus::Filled;
    CPPTEST_ASSERT(outcome.occurrences.front().result.entry_time.has_value() == entered &&
                   outcome.occurrences.front().result.entry_price.has_value() == entered);
    CPPTEST_ASSERT(outcome.occurrences.front().result.exit_time.has_value() == entered &&
                   outcome.occurrences.front().result.exit_price.has_value() == entered);
    if (!entered) {
        CPPTEST_ASSERT(outcome.occurrences.front().result.gross_profit_loss == 0 &&
                       outcome.occurrences.front().result.net_profit_loss == 0 &&
                       outcome.occurrences.front().result.return_percentage == 0 &&
                       outcome.occurrences.front().result.projection.markers.empty());
    } else {
        CPPTEST_ASSERT(outcome.occurrences.front().result.projection.markers.size() == 2 &&
                       outcome.occurrences.front().result.projection.segments.size() == 3 &&
                       outcome.occurrences.front().result.projection.markers[0].kind ==
                           Strategy::Core::ProjectionKind::EntryMarker &&
                       outcome.occurrences.front().result.projection.markers[1].kind ==
                           Strategy::Core::ProjectionKind::ExitMarker);
    }
    const auto& result = outcome.occurrences.front().result;
    if (std::string_view{scenario.name} == "signal-not-filled") {
        CPPTEST_ASSERT(
            result.events.size() == 1 && result.events.front().kind == Strategy::Core::StrategyEventKind::EntryArmed &&
            result.events.front().effective_time == parse_utc("2026-01-01T00:30:00Z") && !result.ambiguous_fill);
    } else if (std::string_view{scenario.name} == "target") {
        CPPTEST_ASSERT(result.entry_time == parse_utc("2026-01-01T00:20:00Z") &&
                       result.exit_time == parse_utc("2026-01-01T00:20:00Z") && result.entry_price == 104.0 &&
                       result.exit_price == 107.0 && result.gross_profit_loss == 3.0 && result.net_profit_loss == 3.0 &&
                       result.return_percentage == 300.0 / 104.0 && !result.ambiguous_fill);
    } else if (std::string_view{scenario.name} == "stop-loss") {
        CPPTEST_ASSERT(result.entry_time == parse_utc("2026-01-01T00:10:00Z") &&
                       result.exit_time == parse_utc("2026-01-01T00:10:00Z") && result.entry_price == 101.0 &&
                       result.exit_price == 95.0 && result.gross_profit_loss == -6.0 &&
                       result.net_profit_loss == -6.0 && result.return_percentage == -600.0 / 101.0 &&
                       !result.ambiguous_fill);
    } else if (std::string_view{scenario.name} == "end-of-range") {
        CPPTEST_ASSERT(result.entry_time == parse_utc("2026-01-01T00:10:00Z") &&
                       result.exit_time == parse_utc("2026-01-01T00:30:00Z") && result.entry_price == 101.0 &&
                       result.exit_price == 106.0 && result.gross_profit_loss == 5.0 && result.net_profit_loss == 5.0 &&
                       result.return_percentage == 500.0 / 101.0 && !result.ambiguous_fill);
    }
    CPPTEST_ASSERT(outcome.timings.data_preparation && outcome.timings.indicator_calculation &&
                   outcome.timings.strategy_execution && outcome.timings.total);
    return 0;
}

int run_scenario(const Scenario& scenario) {
    TemporaryDirectory temporary;
    const auto strategy = fixtures() / (std::string{scenario.name} + ".strategy.json");
    const auto data = fixtures() / (std::string{scenario.name} + ".data.json");
    const auto output_path = temporary.path / "report.json";
    std::optional<Strategy::Core::BacktestRequest> request;
    std::optional<Strategy::Core::BacktestOutcome> cli_outcome;
    Application app{{}, [&](const auto& value, const auto& outcome) {
                        request = value;
                        cli_outcome = outcome;
                    }};
    const std::array<std::string, 14> storage{"--strategy",
                                              strategy.string(),
                                              "--subject",
                                              "ACME",
                                              "--from",
                                              "2026-01-01T00:10:00Z",
                                              "--through",
                                              std::string_view{scenario.name} == "target" ? "2026-01-01T01:20:00Z"
                                                                                          : "2026-01-01T00:30:00Z",
                                              "--provider",
                                              "fixture",
                                              "--provider-config",
                                              data.string(),
                                              "--output",
                                              output_path.string()};
    std::vector<std::string_view> arguments;
    for (const auto& item : storage)
        arguments.push_back(item);
    std::ostringstream output, error;
    CPPTEST_ASSERT(app.run(arguments, output, error) == 0 && error.str().empty() && request && cli_outcome);
    CPPTEST_ASSERT(assert_trade_contract(scenario, *cli_outcome) == 0);
    if (std::string_view{scenario.name} == "no-signal") {
        auto unmeasured = *cli_outcome;
        unmeasured.timings = {};
        const auto report = make_report(unmeasured, {"presentation-only", false});
        CPPTEST_ASSERT(report["timings"]["dataPreparationMilliseconds"].is_null() &&
                       report["timings"]["indicatorCalculationMilliseconds"].is_null() &&
                       report["timings"]["strategyExecutionMilliseconds"].is_null() &&
                       report["timings"]["totalRunnerMilliseconds"].is_null());
        Studio::Core::HistoricalBacktest manual{"manual", *request, {}, {}, unmeasured};
        CPPTEST_ASSERT(Studio::Core::present(manual).total_runner == "—");
    }

    Analysis::Adapters::TaLib::Analyzer direct_analyzer;
    FixtureProvider direct_provider{data};
    std::vector<Market::Core::Time::Frame> frames;
    for (const auto& binding : request->strategy_snapshot.series)
        frames.push_back(binding.timeframe);
    const auto direct = Strategy::Core::BacktestRunner{direct_analyzer}.run(*request, direct_provider, frames);
    CPPTEST_ASSERT(assert_trade_contract(scenario, direct) == 0);

    Studio::Core::HistoricalBacktests studio{
        [data, frames](const auto& value, const auto& cancelled, const auto& progress) {
            Analysis::Adapters::TaLib::Analyzer analyzer;
            FixtureProvider provider{data};
            return Strategy::Core::BacktestRunner{analyzer}.run(value, provider, frames, cancelled, progress);
        }};
    CPPTEST_ASSERT(!studio.enqueue(*request).empty());
    while (studio.busy()) {
        studio.apply_pending();
        std::this_thread::yield();
    }
    studio.apply_pending();
    CPPTEST_ASSERT(studio.runs().size() == 1 && studio.runs().front().outcome);
    const auto& studio_outcome = *studio.runs().front().outcome;
    CPPTEST_ASSERT(assert_trade_contract(scenario, studio_outcome) == 0);

    std::ifstream report_file{output_path};
    auto cli_report = nlohmann::json::parse(report_file);
    cli_report["strategy"].erase("sourceFile");
    cli_report.erase("timings");
    CPPTEST_ASSERT(cli_report == canonical(direct) && cli_report == canonical(studio_outcome));

    if (std::string_view{scenario.name} == "target") {
        CPPTEST_ASSERT(direct.occurrences.size() == 3 && direct.summary.signal_count == 3 &&
                       direct.summary.filled_count == 3 && direct.summary.closed_count == 3 &&
                       direct.summary.target_count == 3 && direct.summary.stop_count == 0 &&
                       direct.summary.end_of_range_count == 0 && direct.summary.gross_profit_loss == 9.0 &&
                       direct.summary.costs == 0.0 && direct.summary.net_profit_loss == 9.0);
        const std::array expected_entries{parse_utc("2026-01-01T00:20:00Z"), parse_utc("2026-01-01T00:40:00Z"),
                                          parse_utc("2026-01-01T01:00:00Z")};
        const std::array expected_exits{parse_utc("2026-01-01T00:20:00Z"), parse_utc("2026-01-01T00:40:00Z"),
                                        parse_utc("2026-01-01T01:00:00Z")};
        for (std::size_t index = 0; index < direct.occurrences.size(); ++index) {
            const auto& occurrence = direct.occurrences[index];
            CPPTEST_ASSERT(occurrence.id == index && occurrence.result.entry_time == expected_entries[index] &&
                           occurrence.result.exit_time == expected_exits[index] &&
                           occurrence.result.exit_reason == Strategy::Core::ExitReason::Target &&
                           occurrence.result.entry_price && occurrence.result.exit_price &&
                           *occurrence.result.exit_price == 107 && !occurrence.result.events.empty() &&
                           !occurrence.result.evaluations.empty() && !occurrence.result.projection.markers.empty());
            if (index > 0)
                CPPTEST_ASSERT(direct.occurrences[index - 1].result.exit_time <= occurrence.result.entry_time);
        }
        CPPTEST_ASSERT(direct.inputs.size() == 2 && direct.inputs[0].series.key.instrument == "ACME" &&
                       direct.inputs[1].series.key.instrument == "XLK" &&
                       direct.inputs[0].series.key.timeframe != direct.inputs[1].series.key.timeframe);
        CPPTEST_ASSERT(direct.audit_events.size() == 9 &&
                       std::ranges::all_of(direct.audit_events,
                                           [](const auto& event) { return event.occurrence_id.has_value(); }));
        CPPTEST_ASSERT(!direct.evaluations.empty() && !direct.evaluations.back().occurrence_id &&
                       direct.evaluations.back().evaluation.time == direct.request.through);
        const auto& evidence = direct.audit_events.front().event.evidence;
        const std::vector<std::string> expected_paths{
            "entry-all/subject-ready", "entry-all/ordered-sequence/subject-sequence",
            "entry-all/ordered-sequence/peer-ready", "entry-all/ordered-sequence", "entry-all"};
        std::vector<std::string> actual_paths;
        for (const auto& item : evidence)
            actual_paths.push_back(item.condition_path);
        CPPTEST_ASSERT(actual_paths == expected_paths);
        const auto peer = std::ranges::find(evidence, "entry-all/ordered-sequence/peer-ready",
                                            &Strategy::Core::Evidence::condition_path);
        const Market::Core::Time::Frame peer_frame{1, Market::Core::Time::Unit::Day};
        CPPTEST_ASSERT(peer != evidence.end() && peer->truth == Strategy::Core::Truth::True &&
                       peer->binding_id == "peer-daily" && peer->source_key && peer->source_key->instrument == "XLK" &&
                       peer->source_timeframe == peer_frame && !peer->sequence_step && !peer->sequence_size);
        const auto sequence =
            std::ranges::find(evidence, "entry-all/ordered-sequence", &Strategy::Core::Evidence::condition_path);
        CPPTEST_ASSERT(sequence != evidence.end() && sequence->truth == Strategy::Core::Truth::True &&
                       sequence->sequence_step == 2 && sequence->sequence_size == 2);
    }
    return 0;
}
} // namespace

int test_historical_backtest_acceptance_matrix() {
    using RS = Strategy::Core::BacktestResultStatus;
    using ES = Strategy::Core::EntryStatus;
    using ER = Strategy::Core::ExitReason;
    for (const Scenario scenario :
         std::array{Scenario{"no-signal", RS::NoSignal, ES::NoSignal, ER::None},
                    Scenario{"signal-not-filled", RS::SignalNotFilled, ES::AwaitingFill, ER::None},
                    Scenario{"target", RS::Exited, ES::Filled, ER::Target},
                    Scenario{"stop-loss", RS::Exited, ES::Filled, ER::StopLoss},
                    Scenario{"end-of-range", RS::OpenPositionClosedAtEnd, ES::Filled, ER::EndOfRange}})
        CPPTEST_ASSERT(run_scenario(scenario) == 0);
    return 0;
}

int main() {
    CPPTEST_RUN(test_historical_backtest_acceptance_matrix);
    return 0;
}
