#pragma once

#include <Didrachma/market/core/provider/Capability.h>
#include <Didrachma/market/core/provider/History.h>
#include <Didrachma/market/core/provider/Subscription.h>
#include <memory>

namespace Didrachma::Market::Core::Provider {
class Data {
public:
    virtual ~Data() = default;

    [[nodiscard]] virtual CapabilitySet capabilities() const = 0;
    virtual HistoryResult load_history(const HistoryRequest&) = 0;
    virtual std::unique_ptr<Subscription> subscribe(const Series::Key&, UpdateHandler) = 0;
};
} // namespace Didrachma::Market::Core::Provider
