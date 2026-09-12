#pragma once

#include "data/StockData.h"

#include <Didrachma/market/providers/yahoo/Provider.h>

namespace Didrachma::Apps::Chart::Adapters {
class YahooStockDataAdapter final : public Data::StockData {
    Market::Providers::Yahoo::Provider m_provider;

public:
    Data::TickerData get_ticker(std::string ticker, std::string start, std::string end,
                                Data::Interval interval) override;
};
} // namespace Didrachma::Apps::Chart::Adapters
