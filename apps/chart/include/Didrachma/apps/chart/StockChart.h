#pragma once

#include <Didrachma/apps/chart/App.h>
#include <Didrachma/market/core/StockData.h>
#include <implot.h>
#include <implot_internal.h>
#include <map>

namespace Didrachma::Apps::Chart {

struct StockChart : App {
    using App::App;

    char t1_str[32];
    char t2_str[32];
    ImPlotTime t1;
    ImPlotTime t2;

    Market::Core::StockData& m_data;
    std::map<std::string, Market::Core::TickerData> m_ticker_data;
    std::string m_status;

    StockChart(Market::Core::StockData& data, std::string title, int width, int height, int argc, const char* argv[])
        : App(std::move(title), width, height, argc, argv), m_data(data) {}

    void Start() override;
    void Update() override;
};

} // namespace Didrachma::Apps::Chart
