#pragma once

#include <Didrachma/analysis/core/indicator/Parameter.h>
#include <Didrachma/stockChart/core/Document.h>
#include <map>
#include <optional>

namespace Didrachma::StockChart::Core {
inline constexpr std::uint32_t ProfileFormatVersion = 1;

struct IndicatorTemplate {
    std::string id;
    std::string definition_id;
    bool enabled{true};
    std::map<std::string, Analysis::Core::Indicator::ParameterValue> parameters;
    std::string name;
};

struct LayerTemplate {
    LayerKind kind{LayerKind::Line};
    std::vector<OutputBinding> outputs;
    VisualStyle style;
    LayerPane pane{LayerPane::Price};
};

struct Profile {
    std::string name;
    std::vector<IndicatorTemplate> indicators;
    std::vector<LayerTemplate> layers;
};

struct ProfileError {
    std::string message;
};

class ProfileCollection {
    std::vector<Profile> m_profiles;
    std::optional<std::string> m_default_name;

public:
    [[nodiscard]] std::span<const Profile> profiles() const {
        return m_profiles;
    }
    [[nodiscard]] const std::optional<std::string>& default_name() const {
        return m_default_name;
    }

    std::optional<ProfileError> add(Profile profile);
    std::optional<ProfileError> rename(const std::string& name, std::string replacement);
    std::optional<ProfileError> update(const std::string& name, Profile profile);
    bool remove(const std::string& name);
    std::optional<ProfileError> set_default(std::optional<std::string> name);
    [[nodiscard]] const Profile* find(const std::string& name) const {
        const auto found = std::ranges::find_if(m_profiles, [&](const auto& profile) { return profile.name == name; });
        return found == m_profiles.end() ? nullptr : &*found;
    }
};

std::optional<ProfileError> validate(const Profile& profile);
Profile capture_profile(const Document& document, std::string name);
std::optional<ProfileError> apply_profile(const Profile& profile, Document& document);
std::string serialize_profiles(const ProfileCollection& profiles);
std::variant<ProfileCollection, ProfileError> deserialize_profiles(const std::string& text);
} // namespace Didrachma::StockChart::Core
