#include "Fixture.h"
#include "TestAssert.h"

#include <Didrachma/strategy/core/Repository.h>
#include <filesystem>

using namespace Didrachma::Strategy::Core;
int test_exact_json_round_trip_and_file_repository() {
    const auto value = Testing::representative();
    const auto first = serialize(value);
    const auto decoded = deserialize(first, Testing::catalog());
    CPPTEST_ASSERT(std::holds_alternative<Definition>(decoded));
    CPPTEST_ASSERT(serialize(std::get<Definition>(decoded)) == first);
    const auto path = std::filesystem::temp_directory_path() / "didrachma-strategy-phase2.json";
    JsonFileRepository repository(path, Testing::catalog());
    CPPTEST_ASSERT(repository.save(value).empty());
    const auto loaded = repository.load();
    CPPTEST_ASSERT(std::holds_alternative<Definition>(loaded));
    CPPTEST_ASSERT(serialize(std::get<Definition>(loaded)) == first);
    std::filesystem::remove(path);
    return 0;
}

int test_future_version_and_malformed_data_are_structured_errors() {
    auto json = serialize(Testing::representative());
    const auto position = json.find("\"formatVersion\": 2");
    json.replace(position, 18, "\"formatVersion\": 99");
    const auto future = deserialize(json, Testing::catalog());
    CPPTEST_ASSERT(std::holds_alternative<std::vector<RepositoryError>>(future));
    CPPTEST_ASSERT(std::get<std::vector<RepositoryError>>(future)[0].path == "/formatVersion");
    const auto malformed = deserialize("{\"formatVersion\":1}", Testing::catalog());
    CPPTEST_ASSERT(std::holds_alternative<std::vector<RepositoryError>>(malformed));
    return 0;
}

int test_readable_example_is_loadable() {
    const auto path =
        std::filesystem::path{__FILE__}.parent_path().parent_path() / "examples" / "momentum-peer.strategy.json";
    JsonFileRepository repository(path, Testing::catalog());
    const auto loaded = repository.load();
    CPPTEST_ASSERT(std::holds_alternative<Definition>(loaded));
    CPPTEST_ASSERT(std::get<Definition>(loaded).series[2].instrument.symbol == "XLK");
    return 0;
}

int test_old_entry_price_is_explicitly_migrated_with_warning() {
    auto legacy = serialize(Testing::representative());
    legacy.replace(legacy.find("\"formatVersion\": 2"), 18, "\"formatVersion\": 1");
    const auto order_begin = legacy.find("\"order\"");
    const auto order_end = legacy.find('}', order_begin);
    legacy.replace(order_begin, order_end - order_begin + 1,
                   "\"price\": {\n      \"kind\": \"absolute\",\n      \"value\": 123.0\n    }");
    const auto decoded = deserialize(legacy, Testing::catalog());
    CPPTEST_ASSERT(std::holds_alternative<MigratedDefinition>(decoded));
    const auto& migrated = std::get<MigratedDefinition>(decoded);
    CPPTEST_ASSERT(migrated.definition.entry.order.kind == EntryOrderKind::NextBarOpen);
    CPPTEST_ASSERT(!migrated.warnings.empty() && migrated.warnings[0].path == "/entry/price");
    return 0;
}

int test_backtest_request_exact_round_trip() {
    BacktestRequest request{Testing::representative(),
                            "AAPL",
                            Didrachma::Market::Core::Time::UtcTimestamp{Duration{100}},
                            Didrachma::Market::Core::Time::UtcTimestamp{Duration{200}},
                            {"fixture", {{"dataset", "stable"}}},
                            {2.0, 10000.0, 1.0, 0.1, 0.05},
                            1};
    const auto first = serialize(request);
    const auto decoded = deserialize_backtest_request(first, Testing::catalog());
    CPPTEST_ASSERT(std::holds_alternative<BacktestRequest>(decoded));
    CPPTEST_ASSERT(serialize(std::get<BacktestRequest>(decoded)) == first);
    return 0;
}

int main() {
    CPPTEST_RUN(test_exact_json_round_trip_and_file_repository);
    CPPTEST_RUN(test_future_version_and_malformed_data_are_structured_errors);
    CPPTEST_RUN(test_readable_example_is_loadable);
    CPPTEST_RUN(test_old_entry_price_is_explicitly_migrated_with_warning);
    CPPTEST_RUN(test_backtest_request_exact_round_trip);
    return 0;
}
