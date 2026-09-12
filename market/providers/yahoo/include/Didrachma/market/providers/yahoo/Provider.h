#pragma once

#include <Didrachma/market/core/provider/Data.h>
#include <memory>

namespace Didrachma::Market::Providers::Yahoo {
class Provider final : public Core::Provider::Data {
    class Impl;
    std::unique_ptr<Impl> m_impl;

public:
    Provider();
    ~Provider() override;
    Provider(const Provider&) = delete;
    Provider(Provider&&) noexcept;

    Provider& operator=(const Provider&) = delete;
    Provider& operator=(Provider&&) noexcept;

    [[nodiscard]] Core::Provider::CapabilitySet capabilities() const override {
        return Core::Provider::CapabilitySet::from(Core::Provider::Capability::History);
    }
    Core::Provider::HistoryResult load_history(const Core::Provider::HistoryRequest&) override;
    std::unique_ptr<Core::Provider::Subscription> subscribe(const Core::Series::Key&,
                                                            Core::Provider::UpdateHandler) override {
        return {};
    }
};
} // namespace Didrachma::Market::Providers::Yahoo
