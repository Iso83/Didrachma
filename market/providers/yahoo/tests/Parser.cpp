#include "Parser.h"

#include "TestAssert.h"

#include <fstream>
#include <nlohmann/json.hpp>

using namespace Didrachma::Market::Providers::Yahoo::Intern;

int test_fixture() {
    std::ifstream fixture(DIDRACHMA_YAHOO_FIXTURE_PATH);
    CPPTEST_ASSERT(fixture.is_open());

    const auto& json = nlohmann::json::parse(fixture);

    const auto data = parse_chart(json, {1, Didrachma::Market::Core::Time::Unit::Day});
    CPPTEST_ASSERT(data.size() == 2);
    CPPTEST_ASSERT(data.size() == 2);
    CPPTEST_ASSERT(data[0].open_time.time_since_epoch().count() == 1704067200);
    CPPTEST_ASSERT(data[0].close_time == data[0].open_time + std::chrono::days{1});
    CPPTEST_ASSERT(data[0].open == 100.0);
    CPPTEST_ASSERT(data[1].close == 107.0);
    CPPTEST_ASSERT(data[1].volume == 1200.0);
    return 0;
}

int test_subday_timestamp_and_close_time() {
    auto json = nlohmann::json::parse(
        R"({"chart":{"result":[{"timestamp":[1704067237],"indicators":{"quote":[{"open":[1],"high":[2],"low":[0],"close":[1.5],"volume":[3]}]}}],"error":null}})");
    const auto data = parse_chart(json, {10, Didrachma::Market::Core::Time::Unit::Minute});
    CPPTEST_ASSERT(data[0].open_time.time_since_epoch().count() == 1704067237);
    CPPTEST_ASSERT(data[0].close_time == data[0].open_time + std::chrono::minutes{10});
    return 0;
}

int test_ten_minute_history_uses_timestamp_aligned_five_minute_bars() {
    const auto json = nlohmann::json::parse(
        R"({"chart":{"result":[{"timestamp":[1800,2100,3000],"indicators":{"quote":[{"open":[100,102,105],"high":[103,106,108],"low":[99,101,104],"close":[102,105,107],"volume":[1000,1500,500]}]}}],"error":null}})");
    const auto data = parse_history(json, {10, Didrachma::Market::Core::Time::Unit::Minute});
    CPPTEST_ASSERT(data.size() == 2);
    CPPTEST_ASSERT(data[0].open_time.time_since_epoch() == std::chrono::minutes{30});
    CPPTEST_ASSERT(data[0].open == 100 && data[0].high == 106 && data[0].low == 99 && data[0].close == 105 &&
                   data[0].volume == 2500);
    CPPTEST_ASSERT(data[1].open_time.time_since_epoch() == std::chrono::minutes{50});
    CPPTEST_ASSERT(data[0].close_time == data[0].open_time + std::chrono::minutes{10});
    return 0;
}

int test_empty_result_and_yahoo_error() {
    const auto empty = nlohmann::json::parse(
        R"({"chart":{"result":[{"indicators":{"quote":[{"open":[],"high":[],"low":[],"close":[],"volume":[]}]}}],"error":null}})");
    CPPTEST_ASSERT(parse_chart(empty, {1, Didrachma::Market::Core::Time::Unit::Day}).empty());

    const auto failed = nlohmann::json::parse(
        R"({"chart":{"result":null,"error":{"code":"Bad Request","description":"Invalid period"}}})");
    try {
        (void)parse_chart(failed, {1, Didrachma::Market::Core::Time::Unit::Day});
    } catch (const std::runtime_error& error) {
        CPPTEST_ASSERT(std::string{error.what()}.find("Bad Request: Invalid period") != std::string::npos);
        return 0;
    }
    CPPTEST_ASSERT(false);
    return 0;
}

int test_full_session_and_missing_bucket_do_not_shift() {
    nlohmann::json json = {{"chart", {{"error", nullptr}, {"result", nlohmann::json::array()}}}};
    nlohmann::json timestamps = nlohmann::json::array();
    nlohmann::json values = nlohmann::json::array();
    for (int index = 0; index < 78; ++index) {
        timestamps.push_back(34200 + index * 300);
        values.push_back(index + 1);
    }
    json["chart"]["result"].push_back(
        {{"timestamp", timestamps},
         {"indicators",
          {{"quote",
            nlohmann::json::array(
                {{{"open", values}, {"high", values}, {"low", values}, {"close", values}, {"volume", values}}})}}}});
    const auto complete = parse_history(json, {10, Didrachma::Market::Core::Time::Unit::Minute});
    CPPTEST_ASSERT(complete.size() == 39);

    json["chart"]["result"][0]["timestamp"].erase(3);
    for (const auto* key : {"open", "high", "low", "close", "volume"})
        json["chart"]["result"][0]["indicators"]["quote"][0][key].erase(3);
    const auto missing = parse_history(json, {10, Didrachma::Market::Core::Time::Unit::Minute});
    CPPTEST_ASSERT(missing.size() == 39);
    CPPTEST_ASSERT(missing[2].open_time.time_since_epoch() == std::chrono::seconds{35400});
    return 0;
}

int main() {
    CPPTEST_RUN(test_fixture);
    CPPTEST_RUN(test_subday_timestamp_and_close_time);
    CPPTEST_RUN(test_ten_minute_history_uses_timestamp_aligned_five_minute_bars);
    CPPTEST_RUN(test_empty_result_and_yahoo_error);
    CPPTEST_RUN(test_full_session_and_missing_bucket_do_not_shift);
    return 0;
}
