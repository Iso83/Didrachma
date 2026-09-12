#include "Parser.h"

#include <Didrachma/market/providers/yahoo/Provider.h>
#include <curl/curl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace Didrachma::Market::Core::Provider;
using namespace Didrachma::Market::Core::Time;
using namespace Didrachma::Market::Core::Series;
namespace fs = std::filesystem;

namespace Didrachma::Market::Providers::Yahoo {
std::time_t unix_time(UtcTimestamp timestamp) {
    return std::chrono::system_clock::to_time_t(timestamp);
}

const char* interval(Frame timeframe) {
    if (timeframe == Frame{1, Unit::Day})
        return "1d";

    if (timeframe == Frame{1, Unit::Hour})
        return "1h";

    return nullptr;
}

class Provider::Impl {
public:
    CURL* curl{};
    std::string error;

    Impl() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK || !(curl = curl_easy_init()))
            throw std::runtime_error("Unable to initialize libcurl");
    }
    ~Impl() {
        curl_easy_cleanup(curl);
        curl_global_cleanup();
    }

    std::string build_url(const HistoryRequest& request);
    bool download(const std::string& url, const fs::path& filename);
};

std::string Provider::Impl::build_url(const HistoryRequest& request) {
    const auto* period = interval(request.key.timeframe);
    if (!period)
        return {};

    char* encoded =
        curl_easy_escape(curl, request.key.instrument.c_str(), static_cast<int>(request.key.instrument.size()));
    if (!encoded)
        return {};

    const auto url = std::format("https://query1.finance.yahoo.com/v8/finance/chart/"
                                 "{}?period1={}&period2={}&interval={}&events=history&includeAdjustedClose=true",
                                 encoded, unix_time(request.range.begin), unix_time(request.range.end), period);
    curl_free(encoded);

    return url;
}

std::size_t write_file(char* data, std::size_t size, std::size_t count, void* userdata) {
    auto& output = *static_cast<std::ofstream*>(userdata);

    const auto bytes = size * count;
    output.write(data, static_cast<std::streamsize>(bytes));

    return output ? bytes : 0;
}

bool Provider::Impl::download(const std::string& url, const fs::path& filename) {
    auto temporary = filename;
    temporary += ".part";

    std::ofstream output(temporary, std::ios::binary);
    if (!output) {
        error = "Could not create cache file";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 Didrachma/0.1");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);

    const auto result = curl_easy_perform(curl);

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    output.close();

    if (result != CURLE_OK || status != 200) {
        fs::remove(temporary);
        error = "Yahoo Finance download failed";
        return false;
    }

    std::error_code ec;
    fs::rename(temporary, filename, ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    return true;
}

Provider::Provider() : m_impl(std::make_unique<Impl>()) {}
Provider::~Provider() = default;
Provider::Provider(Provider&&) noexcept = default;

Provider& Provider::operator=(Provider&&) noexcept = default;

HistoryResult Provider::load_history(const HistoryRequest& request) {
    const auto url = m_impl->build_url(request);
    if (url.empty())
        return Error{"Yahoo Finance does not support this timeframe"};

    const auto filename = request.key.instrument + "_" + std::to_string(unix_time(request.range.begin)) + "_" +
                          std::to_string(unix_time(request.range.end)) + ".json";

    if (!fs::exists(filename) && !m_impl->download(url, filename))
        return Error{m_impl->error};
    try {
        std::ifstream input(filename);
        const auto& json = nlohmann::json::parse(input);
        return Intern::parse_chart(json);
    } catch (const std::exception& e) {
        fs::remove(filename);
        return Error{e.what()};
    }
}
} // namespace Didrachma::Market::Providers::Yahoo
