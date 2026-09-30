#pragma once

#include <Didrachma/strategy/core/Backtest.h>
#include <Didrachma/strategy/core/Repository.h>
#include <filesystem>
#include <variant>

namespace Didrachma::Studio::Core {
struct BacktestSetupValues {
    std::string subject;
    std::string from_date;
    std::string through_date;
    std::string provider_id{"yahoo"};
    Strategy::Core::ExecutionCosts execution;
    std::uint32_t fill_model_version{1};
};

struct BacktestSetupValidation {
    std::optional<Strategy::Core::BacktestRequest> request;
    Strategy::Core::BacktestPlan preview;
    std::vector<Strategy::Core::BacktestError> errors;
};

class BacktestSetup {
    Strategy::Core::Definition m_definition;
    BacktestSetupValues m_values;
    std::optional<Strategy::Core::BacktestRequest> m_submission;

public:
    explicit BacktestSetup(Strategy::Core::Definition, BacktestSetupValues = {});

    [[nodiscard]] const Strategy::Core::Definition& definition() const {
        return m_definition;
    }
    [[nodiscard]] BacktestSetupValues& values() {
        return m_values;
    }
    [[nodiscard]] const BacktestSetupValues& values() const {
        return m_values;
    }
    [[nodiscard]] const std::optional<Strategy::Core::BacktestRequest>& submission() const {
        return m_submission;
    }

    [[nodiscard]] bool requires_subject() const;

    [[nodiscard]] BacktestSetupValidation validate(Analysis::Core::Indicator::Analyzer&,
                                                   std::span<const Strategy::Core::BacktestSourceCapability>,
                                                   Market::Core::Time::UtcTimestamp now,
                                                   bool definition_uncommitted) const;
    bool submit(Analysis::Core::Indicator::Analyzer&, std::span<const Strategy::Core::BacktestSourceCapability>,
                Market::Core::Time::UtcTimestamp now, bool definition_uncommitted);
};

using RecentBacktestSetupLoad = std::variant<BacktestSetupValues, std::vector<Strategy::Core::RepositoryError>>;

class RecentBacktestSetupRepository {
    std::filesystem::path m_path;

public:
    explicit RecentBacktestSetupRepository(std::filesystem::path path) : m_path(std::move(path)) {}
    [[nodiscard]] std::vector<Strategy::Core::RepositoryError> save(const BacktestSetupValues&) const;
    [[nodiscard]] RecentBacktestSetupLoad load() const;
};
} // namespace Didrachma::Studio::Core
