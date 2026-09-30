#pragma once

#include <filesystem>
#include <string>

namespace Didrachma::Strategy::Core::Intern {
[[nodiscard]] std::filesystem::path unique_temporary_sibling(const std::filesystem::path&);
[[nodiscard]] std::string replace_file(const std::filesystem::path& temporary,
                                       const std::filesystem::path& destination);
} // namespace Didrachma::Strategy::Core::Intern
