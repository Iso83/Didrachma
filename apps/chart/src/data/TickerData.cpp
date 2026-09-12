#include "TickerData.h"

#include <implot.h>
#include <implot_internal.h>

namespace Didrachma::Apps::Chart::Data {
void TickerData::reserve(int n) {
    time.reserve(n);
    open.reserve(n);
    high.reserve(n);
    low.reserve(n);
    close.reserve(n);
    volume.reserve(n);
}

void TickerData::push_back(double t, double o, double h, double l, double c, double v) {
    time.push_back(t);
    open.push_back(o);
    high.push_back(h);
    low.push_back(l);
    close.push_back(c);
    volume.push_back(v);

    int s = size();
    int i = std::max(0, s - 20);
    double mean = ImMean(&close[i], std::min(s, 20));
    double stdv = s > 1 ? ImStdDev(&close[i], std::min(s, 20)) : 0;

    bollinger_top.push_back(mean + 2 * stdv);
    bollinger_mid.push_back(mean);
    bollinger_bot.push_back(mean - 2 * stdv);
}
} // namespace Didrachma::Apps::Chart::Legacy
