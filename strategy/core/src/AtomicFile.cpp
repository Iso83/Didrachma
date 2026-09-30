#include "AtomicFile.h"

#include <atomic>
#include <chrono>
#include <system_error>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Didrachma::Strategy::Core::Intern {
std::filesystem::path unique_temporary_sibling(const std::filesystem::path& destination) {
    static std::atomic_uint64_t sequence{};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() / (destination.filename().string() + ".tmp-" + std::to_string(stamp) + "-" +
                                        std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
}

std::string replace_file(const std::filesystem::path& temporary, const std::filesystem::path& destination) {
#ifdef _WIN32
    const auto flags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
    if (MoveFileExW(temporary.c_str(), destination.c_str(), flags))
        return {};

    return std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    return error ? error.message() : std::string{};
#endif
}
} // namespace Didrachma::Strategy::Core::Intern
