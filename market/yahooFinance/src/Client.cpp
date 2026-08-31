#define NOMINMAX
#include <Didrachma/market/yahooFinance/Client.h>
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <implot.h>
#include <implot_internal.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace Didrachma::Market::Core;

namespace Didrachma::Market::YahooFinance {
Client::Client() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        throw std::runtime_error("Unable to initialize libcurl");

    const auto* version = curl_version_info(CURLVERSION_NOW);
    bool supports_https = false;
    if (version != nullptr && version->protocols != nullptr) {
        for (const char* const* protocol = version->protocols; *protocol != nullptr; ++protocol)
            supports_https = supports_https || std::string_view(*protocol) == "https";
    }

    if (!supports_https) {
        curl_global_cleanup();
        throw std::runtime_error("libcurl was built without HTTPS support; enable Schannel on Windows");
    }

    m_curl = curl_easy_init();
    if (m_curl == nullptr) {
        curl_global_cleanup();
        throw std::runtime_error("Unable to create a libcurl session");
    }
}

Client ::~Client() {
    curl_easy_cleanup(m_curl);
    curl_global_cleanup();
}

TickerData Client::get_ticker(std::string ticker, std::string start_date, std::string end_date, Interval interval) {
    std::transform(ticker.begin(), ticker.end(), ticker.begin(), ::toupper);

    std::string filename = ticker + "_" + start_date + "_" + end_date + ".json";

    if (!fs::exists(filename)) {
        std::cout << "Downloading " << filename << '\n';
        if (!download_file(build_url(ticker, start_date, end_date, interval), filename))
            return TickerData("ERROR");
    } else
        std::cout << "Reloading " << filename << '\n';

    try {
        std::ifstream input(filename);
        const auto response = nlohmann::json::parse(input);
        const auto& chart = response.at("chart");
        if (!chart.at("error").is_null()) {
            const auto& error = chart.at("error");
            throw std::runtime_error(error.value("description", "Yahoo Finance returned an error"));
        }

        const auto& result = chart.at("result");
        if (!result.is_array() || result.empty())
            throw std::runtime_error("Yahoo Finance returned no chart result");

        const auto& series = result.at(0);
        const auto& timestamps = series.at("timestamp");
        const auto& quote = series.at("indicators").at("quote").at(0);
        const auto& opens = quote.at("open");
        const auto& highs = quote.at("high");
        const auto& lows = quote.at("low");
        const auto& closes = quote.at("close");
        const auto& volumes = quote.at("volume");
        const auto count =
            std::min({timestamps.size(), opens.size(), highs.size(), lows.size(), closes.size(), volumes.size()});

        TickerData data(ticker);
        data.reserve(static_cast<int>(count));
        for (std::size_t i = 0; i < count; ++i) {
            if (timestamps[i].is_null() || opens[i].is_null() || highs[i].is_null() || lows[i].is_null() ||
                closes[i].is_null() || volumes[i].is_null())
                continue;

            const auto timestamp = timestamps[i].get<std::int64_t>();
            const double day =
                ImPlot::FloorTime(ImPlotTime::FromDouble(static_cast<double>(timestamp)), ImPlotTimeUnit_Day)
                    .ToDouble();
            data.push_back(day, opens[i].get<double>(), highs[i].get<double>(), lows[i].get<double>(),
                           closes[i].get<double>(), volumes[i].get<double>());
        }
        if (data.size() == 0)
            throw std::runtime_error("Yahoo Finance returned no usable price rows");

        m_error.clear();
        return data;

    } catch (const std::exception& error) {
        m_error = std::format("Could not read {}: {}", filename, error.what());
        std::cerr << m_error << '\n';

        fs::remove(filename);
        return TickerData("ERROR");
    }
}

std::string Client::build_url(std::string ticker, std::string start_date, std::string end_date, Interval interval) {
    static const std::string interval_str[]{"1d", "1wk", "1mo"};
    char* encoded_ticker = curl_easy_escape(m_curl, ticker.c_str(), static_cast<int>(ticker.size()));
    if (encoded_ticker == nullptr) {
        curl_free(encoded_ticker);
        m_error = "Yahoo Finance URL encoding failed";
        return {};
    }

    const auto url = std::format("https://query1.finance.yahoo.com/v8/finance/chart/"
                                 "{}?period1={}&period2={}&interval={}&events=history&includeAdjustedClose=true",
                                 encoded_ticker, timestamp_from_string(start_date),
                                 timestamp_from_string(end_date) + 24 * 60 * 60, interval_str[interval]);
    curl_free(encoded_ticker);
    return url;
}

std::time_t Client::timestamp_from_string(std::string date, const char* format) {
    std::tm time{};
    std::istringstream ss(date);
    ss >> std::get_time(&time, format);

    if (ss.fail()) {
        std::cerr << "ERROR: Cannot parse date string (" << date << "); required format %Y-%m-%d\n";

        throw std::invalid_argument(std::format("Invalid date '{}'; expected YYYY-MM-DD", date));
    }

    time.tm_hour = 0;
    time.tm_min = 0;
    time.tm_sec = 0;

#ifdef _WIN32
    return _mkgmtime(&time);
#else
    return timegm(&time);
#endif
}

void Client::configure_request(const std::string& url) {
    curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(m_curl, CURLOPT_USERAGENT, "Mozilla/5.0 Didrachma/0.1");
    curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(m_curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(m_curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(m_curl, CURLOPT_NOSIGNAL, 1L);
}

bool Client::download_file(const std::string& url, const std::string& filename) {
    if (url.empty())
        return false;

    const fs::path destination(filename);
    const fs::path temporary = destination.string() + ".part";

    FILE* file = fopen(temporary.string().c_str(), "wb");
    if (file == nullptr) {
        m_error = std::format("Could not create {}", temporary.string());
        return false;
    }

    configure_request(url);
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, nullptr);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, file);

    const auto result = curl_easy_perform(m_curl);

    long status = 0;
    curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &status);
    fclose(file);

    if (result != CURLE_OK || status != 200 || fs::file_size(temporary) == 0) {
        fs::remove(temporary);

        m_error = std::format("Yahoo Finance download failed (HTTP {}, {})", status,
                              result == CURLE_OK ? "unexpected response" : curl_easy_strerror(result));

        std::cerr << m_error << '\n';
        return false;
    }

    std::error_code error;
    fs::rename(temporary, destination, error);

    if (error) {
        fs::remove(temporary);
        m_error = std::format("Could not store {}: {}", filename, error.message());
        return false;
    }

    m_error.clear();
    return true;
}
} // namespace Didrachma::Market::YahooFinance
