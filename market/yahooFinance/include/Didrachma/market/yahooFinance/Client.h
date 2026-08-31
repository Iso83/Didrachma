#pragma once

#include <Didrachma/market/core/StockData.h>
#include <ctime>
#include <curl/curl.h>

namespace Didrachma::Market::YahooFinance {

class Client : public Core::StockData {
private:
    CURL* m_curl{};

public:
    Client();
    ~Client();
    Client(const Client&) = delete;

    Client& operator=(const Client&) = delete;

    Core::TickerData get_ticker(std::string ticker, std::string start_date, std::string end_date,
                                Core::Interval interval) override;

private:
    std::string build_url(std::string ticker, std::string start_date, std::string end_date, Core::Interval interval);
    std::time_t timestamp_from_string(std::string date, const char* format = "%Y-%m-%d");
    void configure_request(const std::string& url);
    bool download_file(const std::string& url, const std::string& filename);
};

} // namespace Didrachma::Market::YahooFinance
