#include "FixtureProvider.h"

#include "UtcTime.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace Didrachma::Apps::Strategy {
using namespace Market::Core;

struct FixtureProvider::Implementation {
    nlohmann::json document;
};

FixtureProvider::FixtureProvider(const std::filesystem::path& path)
    : m_implementation(std::make_unique<Implementation>()) {
    std::ifstream input{path};
    if (!input)
        throw std::runtime_error("Cannot open fixture provider configuration: " + path.string());

    input >> m_implementation->document;
}

FixtureProvider::~FixtureProvider() = default;

Provider::CapabilitySet FixtureProvider::capabilities() const {
    return Provider::CapabilitySet::from(Provider::Capability::History);
}

Provider::HistoryResult FixtureProvider::load_history(const Provider::HistoryRequest& request) {
    for (const auto& series : m_implementation->document.at("series")) {
        const auto& frame = series.at("timeframe");
        if (series.at("providerId") != request.key.provider || series.at("instrument") != request.key.instrument ||
            frame.at("quantity") != request.key.timeframe.quantity ||
            frame.at("unit") != (request.key.timeframe.unit == Time::Unit::Minute ? "minute"
                                 : request.key.timeframe.unit == Time::Unit::Hour ? "hour"
                                                                                  : "day"))
            continue;

        std::vector<Series::Bar> result;
        for (const auto& item : series.at("bars")) {
            const auto open = parse_utc(item.at("openTime").get<std::string>());
            const auto close = parse_utc(item.at("closeTime").get<std::string>());
            if (!open || !close)
                return Provider::Error{"Fixture contains an invalid UTC timestamp", "invalid_fixture"};

            if (*close < request.range.begin || *close >= request.range.end)
                continue;

            result.push_back({*open, *close, item.at("open"), item.at("high"), item.at("low"), item.at("close"),
                              item.value("volume", 0.0), Series::BarState::Closed});
        }
        return result;
    }
    return Provider::Error{"No fixture data for requested series", "missing_data"};
}

std::unique_ptr<Provider::Subscription> FixtureProvider::subscribe(const Series::Key&, Provider::UpdateHandler) {
    return {};
}
} // namespace Didrachma::Apps::Strategy
