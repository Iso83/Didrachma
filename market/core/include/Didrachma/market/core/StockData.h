#pragma once

#include <Didrachma/market/core/TickerData.h>

namespace Didrachma::Market::Core {
class StockData {
protected:
    std::string m_error;

public:
    virtual ~StockData() = default;

    const std::string& error() const {
        return m_error;
    }

    virtual TickerData get_ticker(std::string ticker, std::string start_date, std::string end_date,
                                  Interval interval) = 0;
};
} // namespace Didrachma::Market::Core
