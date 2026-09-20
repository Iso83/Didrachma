#include "Application.h"
#include "TestAssert.h"

#include <Didrachma/market/core/series/Bar.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace Didrachma;
using namespace Didrachma::Apps::Strategy;

namespace {
using Bar = Market::Core::Series::Bar;
using Timestamp = Market::Core::Time::UtcTimestamp;

struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        static std::atomic_uint64_t sequence{};
        path = std::filesystem::temp_directory_path() /
               ("didrachma-strategy-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(sequence++));
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::filesystem::remove_all(path);
    }
};

std::filesystem::path fixtures() {
    return DIDRACHMA_STRATEGY_FIXTURE_PATH;
}

std::vector<std::string> arguments(const std::filesystem::path& strategy, const std::filesystem::path& data,
                                   const std::filesystem::path& output, std::string from = "2026-01-01T00:10:00Z",
                                   std::string through = "2026-01-01T00:20:00Z", bool subject = true) {
    std::vector<std::string> result{"--strategy", strategy.string()};
    if (subject)
        result.insert(result.end(), {"--subject", "ACME"});
    result.insert(result.end(), {"--from", std::move(from), "--through", std::move(through), "--provider", "fixture",
                                 "--provider-config", data.string(), "--output", output.string()});
    return result;
}

int invoke(const Application& application, const std::vector<std::string>& values, std::ostringstream& output,
           std::ostringstream& error) {
    std::vector<std::string_view> views;
    for (const auto& value : values)
        views.push_back(value);

    return application.run(views, output, error);
}

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream input{path};
    return nlohmann::json::parse(input);
}

struct ProviderState {
    std::map<std::string, int> loads;
};

class CountingProvider final : public Market::Core::Provider::Data {
    std::shared_ptr<ProviderState> m_state;
    std::vector<Bar> m_subject;
    std::vector<Bar> m_peer;

public:
    CountingProvider(std::shared_ptr<ProviderState> state, std::vector<Bar> subject, std::vector<Bar> peer)
        : m_state(std::move(state)), m_subject(std::move(subject)), m_peer(std::move(peer)) {}

    Market::Core::Provider::CapabilitySet capabilities() const override {
        return Market::Core::Provider::CapabilitySet::from(Market::Core::Provider::Capability::History);
    }
    Market::Core::Provider::HistoryResult load_history(const Market::Core::Provider::HistoryRequest& request) override {
        ++m_state->loads[request.key.instrument];
        std::vector<Bar> result;
        const auto& source = request.key.instrument == "ACME" ? m_subject : m_peer;
        for (const auto& bar : source)
            if (bar.close_time && *bar.close_time >= request.range.begin && *bar.close_time < request.range.end)
                result.push_back(bar);

        return result;
    }
    std::unique_ptr<Market::Core::Provider::Subscription> subscribe(const Market::Core::Series::Key&,
                                                                    Market::Core::Provider::UpdateHandler) override {
        return {};
    }
};

Timestamp at(std::chrono::sys_days day, int minutes = 0) {
    return day + std::chrono::minutes{minutes};
}

Bar bar(Timestamp close, double value) {
    return {close - std::chrono::minutes{10},      close, value, value + 2, value - 2, value, 1000,
            Market::Core::Series::BarState::Closed};
}

std::filesystem::path strategy_with_duplicate_peer(const TemporaryDirectory& temporary, int period) {
    auto document = read_json(fixtures() / "strategy.json");
    auto alias = document["series"][1];
    alias["id"] = "peer-alias";
    document["series"].push_back(alias);
    document["indicators"].push_back({{"id", "peer-sma"},
                                      {"seriesId", "peer-alias"},
                                      {"definitionId", "sma"},
                                      {"parameters", {{"period", period}}}});
    const auto path = temporary.path / "duplicate.strategy.json";
    std::ofstream{path} << document.dump(2);

    return path;
}
} // namespace

int test_successful_subject_plus_fixed_peer_run_and_complete_report() {
    TemporaryDirectory temporary;
    auto values = arguments(fixtures() / "strategy.json", fixtures() / "data.json", temporary.path / "report.json");
    values.insert(values.end(), {"--fixed-cost", "1", "--verbose"});
    std::ostringstream output, error;
    CPPTEST_ASSERT(invoke(Application{}, values, output, error) == 0 && error.str().empty());
    const auto report = read_json(temporary.path / "report.json");
    CPPTEST_ASSERT(report["reportFormatVersion"] == 1 && report["inputs"][0]["instrument"] == "ACME");
    CPPTEST_ASSERT(report["inputs"][1]["instrument"] == "XLK" && report["inputs"][0]["readiness"] == "ready");
    const auto& result = report["result"];
    CPPTEST_ASSERT(result["state"] == "exited" && result["status"] == "exited" && result["exitReason"] == "target");
    CPPTEST_ASSERT(result["entryTime"].is_string() && result["currentPrice"].is_number());
    CPPTEST_ASSERT(result["grossProfitLoss"] == 4.0 && result["netProfitLoss"] == 2.0);
    CPPTEST_ASSERT(result["unrealizedProfitLoss"].is_number() && result["unrealizedReturnPercentage"].is_number());
    CPPTEST_ASSERT(result["durationSeconds"].is_number_integer() && result["elapsedSeconds"].is_number_integer());
    CPPTEST_ASSERT(result["closedBarCount"].is_number_unsigned() && result["maximumFavorableExcursion"].is_number());
    CPPTEST_ASSERT(result["maximumAdverseExcursion"].is_number() && result["ambiguousFill"] == false);
    CPPTEST_ASSERT(result["warnings"].is_array());
    CPPTEST_ASSERT(result["events"][0]["oldState"] == "waiting_for_entry");
    CPPTEST_ASSERT(result["events"][0]["evidence"].is_array() && result["triggerCounts"].is_object());
    CPPTEST_ASSERT(result["events"][0]["evidence"][0]["conditionId"] == "subject-ready");
    CPPTEST_ASSERT(result["events"][0]["evidence"][0]["truth"] == "true");
    CPPTEST_ASSERT(result["events"][0]["evidence"][0]["value"].is_number());
    CPPTEST_ASSERT(result["events"][0]["evidence"][0]["sourceTime"].is_string());
    CPPTEST_ASSERT(result["projection"]["markers"].size() == 2 && result["projection"]["segments"].is_array());
    const auto text = output.str();
    for (const auto expected : {"Input subject-10m: ACME", "Input peer-daily: XLK", "Event entry_filled",
                                "Event exit_triggered", "Result: exited (target)", "Gross P/L: 4.0", "Net P/L: 2.0",
                                "Return: 1.9801980198019802%", "Duration: 0 seconds"})
        CPPTEST_ASSERT(text.find(expected) != std::string::npos);

    return 0;
}

int test_inclusive_through_boundary() {
    TemporaryDirectory temporary;
    const auto values =
        arguments(fixtures() / "strategy.json", fixtures() / "data.json", temporary.path / "report.json");
    std::ostringstream output, error;
    CPPTEST_ASSERT(invoke(Application{}, values, output, error) == 0);
    const auto report = read_json(temporary.path / "report.json");
    CPPTEST_ASSERT(report["request"]["throughInclusive"] == "2026-01-01T00:20:00Z");
    CPPTEST_ASSERT(report["result"]["exitReason"] == "target");
    return 0;
}

int test_no_entry_is_valid() {
    TemporaryDirectory temporary;
    const auto values =
        arguments(fixtures() / "no-entry.strategy.json", fixtures() / "data.json", temporary.path / "report.json");
    std::ostringstream output, error;
    CPPTEST_ASSERT(invoke(Application{}, values, output, error) == 0);
    CPPTEST_ASSERT(read_json(temporary.path / "report.json")["result"]["status"] == "no_entry");
    return 0;
}

int test_invalid_strategy_has_documented_exit_and_diagnostic() {
    TemporaryDirectory temporary;
    const auto values =
        arguments(fixtures() / "invalid.strategy.json", fixtures() / "data.json", temporary.path / "report.json");
    std::ostringstream output, error;
    CPPTEST_ASSERT(invoke(Application{}, values, output, error) == 2);
    CPPTEST_ASSERT(error.str().find("displayName") != std::string::npos);
    return 0;
}

int test_missing_provider_data_has_documented_exit_and_diagnostic() {
    TemporaryDirectory temporary;
    const auto values =
        arguments(fixtures() / "strategy.json", fixtures() / "missing-data.json", temporary.path / "report.json");
    std::ostringstream output, error;
    CPPTEST_ASSERT(invoke(Application{}, values, output, error) == 3);
    CPPTEST_ASSERT(error.str().find("insufficient history") != std::string::npos);
    return 0;
}

int test_missing_subject_has_documented_exit_and_diagnostic() {
    TemporaryDirectory temporary;
    const auto values =
        arguments(fixtures() / "strategy.json", fixtures() / "data.json", temporary.path / "report.json",
                  "2026-01-01T00:10:00Z", "2026-01-01T00:20:00Z", false);
    std::ostringstream output, error;
    CPPTEST_ASSERT(invoke(Application{}, values, output, error) == 2);
    CPPTEST_ASSERT(error.str().find("--subject is required") != std::string::npos);
    return 0;
}

int test_strict_utc_validation_and_canonical_report_values() {
    TemporaryDirectory temporary;
    const auto check_invalid = [&](std::string from, std::string through, std::string diagnostic) {
        const auto values = arguments(fixtures() / "strategy.json", fixtures() / "data.json",
                                      temporary.path / "unused.json", std::move(from), std::move(through));
        std::ostringstream output, error;
        return invoke(Application{}, values, output, error) == 2 && error.str().find(diagnostic) != std::string::npos;
    };
    CPPTEST_ASSERT(check_invalid("not-a-date", "2026-01-01T00:20:00Z", "exact UTC form"));
    CPPTEST_ASSERT(check_invalid("2026-01-01T00:10:00Ztail", "2026-01-01T00:20:00Z", "exact UTC form"));
    CPPTEST_ASSERT(check_invalid("2026-01-01T-1:10:00Z", "2026-01-01T00:20:00Z", "exact UTC form"));
    CPPTEST_ASSERT(check_invalid("2026-01-01T24:00:00Z", "2026-01-01T00:20:00Z", "exact UTC form"));
    CPPTEST_ASSERT(check_invalid("2026-01-01T00:60:00Z", "2026-01-01T00:20:00Z", "exact UTC form"));
    CPPTEST_ASSERT(check_invalid("2026-01-01T00:00:60Z", "2026-01-01T00:20:00Z", "exact UTC form"));
    CPPTEST_ASSERT(check_invalid("2026-01-01T00:21:00Z", "2026-01-01T00:20:00Z", "must not exceed"));
    return 0;
}

int test_unique_series_loading_and_calendar_gap_warmup() {
    using namespace std::chrono;
    TemporaryDirectory temporary;
    const auto strategy = strategy_with_duplicate_peer(temporary, 3);
    auto state = std::make_shared<ProviderState>();
    const std::vector subject{bar(at(sys_days{2026y / January / 1}, 10), 101),
                              bar(at(sys_days{2026y / January / 1}, 20), 104)};
    const std::vector peer{bar(at(sys_days{2025y / December / 20}), 51), bar(at(sys_days{2025y / December / 24}), 52),
                           bar(at(sys_days{2025y / December / 28}), 53), bar(at(sys_days{2026y / January / 1}), 54)};
    Application application{[=](std::string_view, const std::filesystem::path&) {
        return std::make_unique<CountingProvider>(state, subject, peer);
    }};
    const auto values = arguments(strategy, "unused", temporary.path / "report.json");
    std::ostringstream output, error;
    CPPTEST_ASSERT(invoke(application, values, output, error) == 0);
    CPPTEST_ASSERT(state->loads["ACME"] == 1 && state->loads["XLK"] == 2);
    return 0;
}

int test_genuinely_insufficient_history_fails_after_bounded_retries() {
    using namespace std::chrono;
    TemporaryDirectory temporary;
    const auto strategy = strategy_with_duplicate_peer(temporary, 3);
    auto state = std::make_shared<ProviderState>();
    const std::vector subject{bar(at(sys_days{2026y / January / 1}, 10), 101)};
    const std::vector peer{bar(at(sys_days{2025y / December / 28}), 53), bar(at(sys_days{2026y / January / 1}), 54)};
    Application application{[=](std::string_view, const std::filesystem::path&) {
        return std::make_unique<CountingProvider>(state, subject, peer);
    }};
    const auto values = arguments(strategy, "unused", temporary.path / "report.json");
    std::ostringstream output, error;
    CPPTEST_ASSERT(invoke(application, values, output, error) == 3);
    CPPTEST_ASSERT(state->loads["XLK"] == 5 && error.str().find("insufficient history") != std::string::npos);
    return 0;
}

int main() {
    CPPTEST_RUN(test_successful_subject_plus_fixed_peer_run_and_complete_report);
    CPPTEST_RUN(test_inclusive_through_boundary);
    CPPTEST_RUN(test_no_entry_is_valid);
    CPPTEST_RUN(test_invalid_strategy_has_documented_exit_and_diagnostic);
    CPPTEST_RUN(test_missing_provider_data_has_documented_exit_and_diagnostic);
    CPPTEST_RUN(test_missing_subject_has_documented_exit_and_diagnostic);
    CPPTEST_RUN(test_strict_utc_validation_and_canonical_report_values);
    CPPTEST_RUN(test_unique_series_loading_and_calendar_gap_warmup);
    CPPTEST_RUN(test_genuinely_insufficient_history_fails_after_bounded_retries);
    return 0;
}
