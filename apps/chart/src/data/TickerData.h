#pragma once

#include <string>
#include <vector>

namespace Didrachma::Apps::Chart::Data {

enum Interval { Interval_Daily, Interval_Weekly, Interval_Monthly };

class TickerData {
public:
    std::string ticker;
    std::vector<double> time;
    std::vector<double> open;
    std::vector<double> high;
    std::vector<double> low;
    std::vector<double> close;
    std::vector<double> volume;

    std::vector<double> bollinger_top;
    std::vector<double> bollinger_mid;
    std::vector<double> bollinger_bot;

public:
    TickerData() {}
    TickerData(std::string ticker) : ticker(ticker) {}

    int size() const {
        return (int)time.size();
    }

    void reserve(int n);
    void push_back(double t, double o, double h, double l, double c, double v);
};

} // namespace Didrachma::Apps::Chart::Legacy
