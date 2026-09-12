#pragma once

#include <Didrachma/market/core/series/BarUpdate.h>
#include <functional>

namespace Didrachma::Market::Core::Provider {
class Subscription {
public:
    virtual ~Subscription() = default;
};

using UpdateHandler = std::function<void(Series::BarUpdate)>;
} // namespace Didrachma::Market::Core::Provider
