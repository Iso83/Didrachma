#include <Didrachma/stockChart/core/Profile.h>
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <set>

namespace Didrachma::StockChart::Core::Intern {
using Json = nlohmann::json;

Json parameter_to_json(const Analysis::Core::Indicator::ParameterValue& value) {
    return std::visit([](const auto& item) { return Json(item); }, value);
}

Analysis::Core::Indicator::ParameterValue parameter_from_json(const Json& value) {
    if (value.is_boolean())
        return value.get<bool>();

    if (value.is_number_integer())
        return value.get<std::int64_t>();

    if (value.is_number())
        return value.get<double>();

    if (value.is_string())
        return value.get<std::string>();

    throw Json::type_error::create(302, "unsupported parameter value", &value);
}

Json style_to_json(const VisualStyle& style) {
    return {{"color", {style.color.red, style.color.green, style.color.blue, style.color.alpha}},
            {"visible", style.visible},
            {"lineWidth", style.line_width},
            {"bandFillOpacity", style.band_fill_opacity}};
}

VisualStyle style_from_json(const Json& value) {
    const auto color = value.at("color").get<std::vector<float>>();
    if (color.size() != 4)
        throw std::invalid_argument("color must contain four values");

    return {{color[0], color[1], color[2], color[3]},
            value.at("visible").get<bool>(),
            value.at("lineWidth").get<float>(),
            value.at("bandFillOpacity").get<float>()};
}
} // namespace Didrachma::StockChart::Core::Intern

namespace Didrachma::StockChart::Core {
std::optional<ProfileError> validate(const Profile& profile) {
    if (profile.name.empty())
        return ProfileError{"Profile name cannot be empty"};

    std::set<std::string> ids;
    for (const auto& indicator : profile.indicators)
        if (indicator.id.empty() || indicator.definition_id.empty() || !ids.insert(indicator.id).second)
            return ProfileError{"Indicator template ids and definitions must be non-empty and unique"};

    for (const auto& layer : profile.layers) {
        if (layer.outputs.empty() || (layer.kind == LayerKind::Band && layer.outputs.size() != 2))
            return ProfileError{"Layers require outputs and bands require exactly two"};

        if (!(layer.style.line_width > 0.0F) || layer.style.band_fill_opacity < 0.0F ||
            layer.style.band_fill_opacity > 1.0F)
            return ProfileError{"Layer style is outside its valid range"};

        for (const auto& output : layer.outputs)
            if (!ids.contains(output.instance_id) || output.output_id.empty())
                return ProfileError{"Layer binding references an unknown template or empty output"};
    }

    return std::nullopt;
}

Profile capture_profile(const Document& document, std::string name) {
    Profile profile{std::move(name)};
    for (const auto& entry : document.indicators())
        profile.indicators.push_back({entry.instance.id, entry.instance.definition_id, entry.instance.enabled,
                                      entry.instance.parameters, entry.name});

    for (const auto& layer : document.layers())
        profile.layers.push_back({layer.kind, layer.outputs, layer.style, layer.pane});

    return profile;
}

std::optional<ProfileError> ProfileCollection::add(Profile profile) {
    if (auto error = validate(profile))
        return error;

    if (find(profile.name))
        return ProfileError{"Profile names must be unique"};

    m_profiles.push_back(std::move(profile));
    return std::nullopt;
}

std::optional<ProfileError> ProfileCollection::rename(const std::string& name, std::string replacement) {
    auto found = std::ranges::find(m_profiles, name, &Profile::name);
    if (found == m_profiles.end())
        return ProfileError{"Profile does not exist"};

    if (replacement.empty() || (replacement != name && find(replacement)))
        return ProfileError{"Profile name must be non-empty and unique"};

    found->name = replacement;
    if (m_default_name == name)
        m_default_name = std::move(replacement);

    return std::nullopt;
}

std::optional<ProfileError> ProfileCollection::update(const std::string& name, Profile profile) {
    auto found = std::ranges::find(m_profiles, name, &Profile::name);
    if (found == m_profiles.end())
        return ProfileError{"Profile does not exist"};

    profile.name = name;
    if (const auto error = validate(profile))
        return error;

    *found = std::move(profile);
    return std::nullopt;
}

bool ProfileCollection::remove(const std::string& name) {
    if (!std::erase_if(m_profiles, [&](const auto& profile) { return profile.name == name; }))
        return false;

    if (m_default_name == name)
        m_default_name.reset();

    return true;
}

std::optional<ProfileError> ProfileCollection::set_default(std::optional<std::string> name) {
    if (name && !find(*name))
        return ProfileError{"Default profile does not exist"};

    m_default_name = std::move(name);
    return std::nullopt;
}

std::optional<ProfileError> apply_profile(const Profile& profile, Document& document) {
    if (auto error = validate(profile))
        return error;

    // Build the replacement on a copy so validation/allocation failures cannot leave a
    // partially-applied configuration in the live chart.
    auto replacement = document;
    std::vector<std::string> existing;
    for (const auto& entry : replacement.indicators())
        existing.push_back(entry.instance.id);
    for (const auto& id : existing)
        replacement.remove_indicator(id);

    std::map<std::string, std::string> instance_ids;
    for (const auto& item : profile.indicators) {
        const auto id = replacement.add_indicator(item.definition_id, item.parameters);
        replacement.set_indicator_enabled(id, item.enabled);
        if (!item.name.empty())
            replacement.set_indicator_name(id, item.name);
        instance_ids.emplace(item.id, id);
    }

    for (const auto& item : profile.layers) {
        auto outputs = item.outputs;
        for (auto& output : outputs)
            output.instance_id = instance_ids.at(output.instance_id);
        replacement.add_layer(item.kind, std::move(outputs), item.style, item.pane);
    }

    document = std::move(replacement);
    return std::nullopt;
}

std::string serialize_profiles(const ProfileCollection& profiles) {
    using namespace Intern;

    Json root{{"version", ProfileFormatVersion}, {"profiles", Json::array()}};

    if (const auto& default_name = profiles.default_name(); default_name)
        root["defaultProfile"] = *default_name;
    else
        root["defaultProfile"] = nullptr;

    for (const auto& profile : profiles.profiles()) {
        Json encoded{{"name", profile.name}, {"indicators", Json::array()}, {"layers", Json::array()}};
        for (const auto& indicator : profile.indicators) {
            Json parameters = Json::object();
            for (const auto& [name, value] : indicator.parameters)
                parameters[name] = parameter_to_json(value);
            encoded["indicators"].push_back({{"id", indicator.id},
                                             {"definitionId", indicator.definition_id},
                                             {"name", indicator.name},
                                             {"enabled", indicator.enabled},
                                             {"parameters", parameters}});
        }

        for (const auto& layer : profile.layers) {
            Json outputs = Json::array();
            for (const auto& output : layer.outputs)
                outputs.push_back({{"instanceId", output.instance_id}, {"outputId", output.output_id}});
            encoded["layers"].push_back({{"kind", static_cast<int>(layer.kind)},
                                         {"outputs", outputs},
                                         {"style", style_to_json(layer.style)},
                                         {"pane", static_cast<int>(layer.pane)}});
        }
        root["profiles"].push_back(std::move(encoded));
    }

    return root.dump(2);
}

std::variant<ProfileCollection, ProfileError> deserialize_profiles(const std::string& text) {
    using namespace Intern;
    try {
        const auto root = Json::parse(text);
        if (root.at("version").get<std::uint32_t>() != ProfileFormatVersion)
            return ProfileError{"Unsupported profile format version"};

        ProfileCollection collection;
        for (const auto& encoded : root.at("profiles")) {
            Profile profile{encoded.at("name").get<std::string>()};
            for (const auto& item : encoded.at("indicators")) {
                IndicatorTemplate indicator{item.at("id").get<std::string>(),
                                            item.at("definitionId").get<std::string>(), item.at("enabled").get<bool>()};
                indicator.name = item.value("name", indicator.definition_id);
                for (const auto& [name, value] : item.at("parameters").items())
                    indicator.parameters.emplace(name, parameter_from_json(value));
                profile.indicators.push_back(std::move(indicator));
            }

            for (const auto& item : encoded.at("layers")) {
                LayerTemplate layer{
                    static_cast<LayerKind>(item.at("kind").get<int>()), {}, style_from_json(item.at("style"))};
                layer.pane = static_cast<LayerPane>(item.value("pane", static_cast<int>(LayerPane::Price)));
                for (const auto& output : item.at("outputs"))
                    layer.outputs.push_back(
                        {output.at("instanceId").get<std::string>(), output.at("outputId").get<std::string>()});
                profile.layers.push_back(std::move(layer));
            }

            if (auto error = collection.add(std::move(profile)))
                return *error;
        }
        if (!root.at("defaultProfile").is_null())
            if (auto error = collection.set_default(root.at("defaultProfile").get<std::string>()))
                return *error;

        return collection;
    } catch (const std::exception& error) {
        return ProfileError{error.what()};
    }
}
} // namespace Didrachma::StockChart::Core
