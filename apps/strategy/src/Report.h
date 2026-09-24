#pragma once

#include <Didrachma/strategy/core/Backtest.h>
#include <iosfwd>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace Didrachma::Apps::Strategy {
struct ReportContext {
    std::string strategy_file;
    std::string provider;
    std::optional<std::string> subject;
    Market::Core::Time::UtcTimestamp from;
    Market::Core::Time::UtcTimestamp through;
    Didrachma::Strategy::Core::ExecutionCosts costs;
    bool costs_overridden{};
};

[[nodiscard]] nlohmann::json make_report(const Didrachma::Strategy::Core::BacktestOutcome&, const ReportContext&);
void print_summary(const nlohmann::json&, bool verbose, std::ostream&);
} // namespace Didrachma::Apps::Strategy
