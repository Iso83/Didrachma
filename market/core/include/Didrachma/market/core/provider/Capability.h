#pragma once

#include <cstdint>

namespace Didrachma::Market::Core::Provider {
enum class Capability : std::uint32_t { History = 1u << 0, Polling = 1u << 1, Streaming = 1u << 2 };

struct CapabilitySet {
    std::uint32_t value{};

    [[nodiscard]]
    static constexpr CapabilitySet from(Capability capability) {
        return {static_cast<std::uint32_t>(capability)};
    }

    constexpr CapabilitySet operator|(Capability rhs) const {
        return {value | static_cast<std::uint32_t>(rhs)};
    }

    [[nodiscard]]
    constexpr bool supports(Capability capability) const {
        return (value & static_cast<std::uint32_t>(capability)) != 0;
    }
};

constexpr CapabilitySet operator|(Capability lhs, Capability rhs) {
    return {static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs)};
}
} // namespace Didrachma::Market::Core::Provider
