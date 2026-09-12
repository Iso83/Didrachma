#include "adapters/YahooStockDataAdapter.h"
#include "chart/StockChart.h"

int main(int argc, char const* argv[]) {
    Didrachma::Apps::Chart::Adapters::YahooStockDataAdapter yahooStockProvider;
    Didrachma::Apps::Chart::StockChart app(yahooStockProvider, "ImStocks", 960, 540, argc, argv);
    app.Run();
}
