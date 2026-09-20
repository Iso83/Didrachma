#pragma once

#include <Didrachma/market/core/provider/Data.h>
#include <filesystem>

namespace Didrachma::Apps::Strategy {
class FixtureProvider final : public Market::Core::Provider::Data {
    struct Implementation;
    std::unique_ptr<Implementation> m_implementation;

public:
    explicit FixtureProvider(const std::filesystem::path&);
    ~FixtureProvider() override;

    [[nodiscard]] Market::Core::Provider::CapabilitySet capabilities() const override;
    Market::Core::Provider::HistoryResult load_history(const Market::Core::Provider::HistoryRequest&) override;
    std::unique_ptr<Market::Core::Provider::Subscription> subscribe(const Market::Core::Series::Key&,
                                                                    Market::Core::Provider::UpdateHandler) override;
};
} // namespace Didrachma::Apps::Strategy
