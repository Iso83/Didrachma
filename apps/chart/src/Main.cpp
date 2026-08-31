#include <Didrachma/apps/chart/StockChart.h>
#include <Didrachma/market/yahooFinance/Client.h>

int main(int argc, char const* argv[]) {
    Didrachma::Market::YahooFinance::Client yahooStockProvider;

    Didrachma::Apps::Chart::StockChart app(yahooStockProvider, "ImStocks", 960, 540, argc, argv);
    app.Run();
}
