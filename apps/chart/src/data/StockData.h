#pragma once

#include "TickerData.h"

namespace Didrachma::Apps::Chart::Data {
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
} // namespace Didrachma::Apps::Chart::Data
