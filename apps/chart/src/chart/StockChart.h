#pragma once

#include "App.h"
#include "data/StockData.h"
#include "data/TickerData.h"

#include <implot_internal.h>
#include <map>

namespace Didrachma::Apps::Chart {
class StockChart final : public App {
private:
    char t1_str[32];
    char t2_str[32];
    ImPlotTime t1;
    ImPlotTime t2;

    Data::StockData& m_data;
    std::map<std::string, Data::TickerData> m_ticker_data;
    std::string m_status;

public:
    StockChart(Data::StockData& data, std::string title, int width, int height, int argc, const char* argv[]);

protected:
    void Start() override;
    void Update() override;
};
} // namespace Didrachma::Apps::Chart
