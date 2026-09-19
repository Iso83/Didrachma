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
    const auto position = json.find("\"formatVersion\": 1");
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

int main() {
    CPPTEST_RUN(test_exact_json_round_trip_and_file_repository);
    CPPTEST_RUN(test_future_version_and_malformed_data_are_structured_errors);
    CPPTEST_RUN(test_readable_example_is_loadable);
    return 0;
}
