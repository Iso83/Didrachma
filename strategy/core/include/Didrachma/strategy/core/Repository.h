#pragma once

#include <Didrachma/strategy/core/Validation.h>
#include <filesystem>
#include <variant>

namespace Didrachma::Strategy::Core {
inline constexpr std::uint32_t strategy_format_version = 1;

struct RepositoryError {
    std::string path;
    std::string message;
};

using LoadResult = std::variant<Definition, std::vector<RepositoryError>>;

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
} // namespace Didrachma::Strategy::Core
