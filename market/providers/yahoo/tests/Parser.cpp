#include "Parser.h"

#include "TestAssert.h"

#include <fstream>
#include <nlohmann/json.hpp>

using namespace Didrachma::Market::Providers::Yahoo::Intern;

int test_fixture() {
    std::ifstream fixture(DIDRACHMA_YAHOO_FIXTURE_PATH);
    CPPTEST_ASSERT(fixture.is_open());

    const auto& json = nlohmann::json::parse(fixture);

    const auto data = parse_chart(json);
    CPPTEST_ASSERT(data.size() == 2);
    CPPTEST_ASSERT(data.size() == 2);
    CPPTEST_ASSERT(data[0].open_time.time_since_epoch().count() == 1704067200);
    CPPTEST_ASSERT(data[0].open == 100.0);
    CPPTEST_ASSERT(data[1].close == 107.0);
    CPPTEST_ASSERT(data[1].volume == 1200.0);
    return 0;
}

int main() {
    CPPTEST_RUN(test_fixture);
    return 0;
}
