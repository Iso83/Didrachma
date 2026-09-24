#pragma once

#include <Didrachma/strategy/core/Backtest.h>
#include <Didrachma/strategy/core/Validation.h>
#include <filesystem>
#include <variant>

namespace Didrachma::Strategy::Core {
inline constexpr std::uint32_t strategy_format_version = 2;

struct RepositoryError {
    std::string path;
    std::string message;
};

struct RepositoryWarning {
    std::string path;
    std::string message;
};

struct MigratedDefinition {
    Definition definition;
    std::vector<RepositoryWarning> warnings;
};

using LoadResult = std::variant<Definition, MigratedDefinition, std::vector<RepositoryError>>;

class Repository {
public:
    virtual ~Repository() = default;
    [[nodiscard]] virtual std::vector<RepositoryError> save(const Definition&) = 0;
    [[nodiscard]] virtual LoadResult load() const = 0;
};

class JsonFileRepository final : public Repository {
    std::filesystem::path m_path;
    std::vector<Analysis::Core::Indicator::Definition> m_catalog;

public:
    JsonFileRepository(std::filesystem::path path, std::vector<Analysis::Core::Indicator::Definition> catalog);

    [[nodiscard]] std::vector<RepositoryError> save(const Definition&) override;
    [[nodiscard]] LoadResult load() const override;
};

[[nodiscard]] std::string serialize(const Definition&);
[[nodiscard]] LoadResult deserialize(std::string_view, IndicatorCatalog catalog = {});

using BacktestRequestLoadResult = std::variant<BacktestRequest, std::vector<RepositoryError>>;
[[nodiscard]] std::string serialize(const BacktestRequest&);
[[nodiscard]] BacktestRequestLoadResult deserialize_backtest_request(std::string_view, IndicatorCatalog catalog = {});
} // namespace Didrachma::Strategy::Core
