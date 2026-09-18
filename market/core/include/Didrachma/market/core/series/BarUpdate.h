#pragma once

#include <Didrachma/market/core/series/Bar.h>
#include <Didrachma/market/core/series/Key.h>
#include <cstdint>
#include <vector>

namespace Didrachma::Market::Core::Series {
enum class BarUpdateKind { AppendClosed, ReplaceForming, Backfill, Reset };

struct BarUpdate {
    Key key;
    BarUpdateKind kind{BarUpdateKind::AppendClosed};
    std::vector<Bar> bars;
};
} // namespace Didrachma::Market::Core::Series
