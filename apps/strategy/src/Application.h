#pragma once

#include <Didrachma/market/core/provider/Data.h>
#include <Didrachma/strategy/core/Backtest.h>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>
#include <span>
#include <string_view>

namespace Didrachma::Apps::Strategy {
using ProviderFactory = std::function<std::unique_ptr<Market::Core::Provider::Data>(
    std::string_view provider_id, const std::filesystem::path& configuration)>;
using RunObserver = std::function<void(const Didrachma::Strategy::Core::BacktestRequest&,
                                       const Didrachma::Strategy::Core::BacktestOutcome&)>;

class Application {
    ProviderFactory m_provider_factory;
    RunObserver m_run_observer;

public:
    explicit Application(ProviderFactory = {}, RunObserver = {});

    int run(std::span<const std::string_view> arguments, std::ostream& output, std::ostream& error) const;
};
} // namespace Didrachma::Apps::Strategy
