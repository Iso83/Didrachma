#include "DemoData.h"

namespace Didrachma::Apps::Studio {
std::vector<Market::Core::Series::Bar> demo_bars() {
    const Market::Core::Time::UtcTimestamp start{std::chrono::seconds{1700000000}};
    std::vector<Market::Core::Series::Bar> bars;
    for (int index = 0; index < 24; ++index) {
        const double price = 98.0 + index * 0.55 + (index % 4 - 2) * 1.2;
        bars.push_back({start + std::chrono::hours{index}, start + std::chrono::hours{index + 1}, price, price + 2.2,
                        price - 1.7, price + (index % 2 ? 1.0 : -0.7), 1000.0 + index * 80.0});
    }
    return bars;
}

} // namespace Didrachma::Apps::Studio
