#include "TestAssert.h"

#include <Didrachma/stockChart/core/Profile.h>

using namespace Didrachma::StockChart::Core;
using namespace Didrachma::Market::Core;

Profile bollinger_profile() {
    return {"Bollinger",
            {{"bb", "bbands", false, {{"period", std::int64_t{20}}}, "Wide bands"}},
            {{LayerKind::Band, {{"bb", "upper"}, {"bb", "lower"}}, {{0, 0.5F, 1, 1}, true, 2, 0.25F}},
             {LayerKind::Line, {{"bb", "middle"}}, {{1, 1, 1, 1}, false, 1, 0.2F}}}};
}

Document chart(std::string id) {
    return {std::move(id),
            {"fake", "TEST", {1, Time::Unit::Day}},
            {Time::UtcTimestamp{std::chrono::seconds{0}}, Time::UtcTimestamp{std::chrono::seconds{100}}}};
}

int test_default_validation_and_copy_on_apply() {
    ProfileCollection profiles;
    CPPTEST_ASSERT(!profiles.add(bollinger_profile()));
    CPPTEST_ASSERT(!profiles.set_default("Bollinger") && *profiles.default_name() == "Bollinger");
    CPPTEST_ASSERT(profiles.set_default("missing").has_value());
    auto first = chart("first");
    auto second = chart("second");
    CPPTEST_ASSERT(!apply_profile(*profiles.find("Bollinger"), first));
    CPPTEST_ASSERT(!apply_profile(*profiles.find("Bollinger"), second));
    CPPTEST_ASSERT(first.indicators()[0].instance.id != second.indicators()[0].instance.id);
    CPPTEST_ASSERT(first.layers()[0].kind == LayerKind::Band && first.layers()[0].outputs.size() == 2);
    CPPTEST_ASSERT(first.layers()[1].kind == LayerKind::Line && !first.layers()[1].style.visible);
    first.find_indicator(first.indicators()[0].instance.id)->instance.parameters["period"] = std::int64_t{30};
    CPPTEST_ASSERT(std::get<std::int64_t>(second.indicators()[0].instance.parameters.at("period")) == 20);
    CPPTEST_ASSERT(!first.indicators()[0].instance.enabled);
    CPPTEST_ASSERT(first.indicators()[0].name == "Wide bands");
    return 0;
}

int test_versioned_serialization_round_trip_and_rejection() {
    ProfileCollection source;
    CPPTEST_ASSERT(!source.add(bollinger_profile()) && !source.set_default("Bollinger"));
    const auto text = serialize_profiles(source);
    CPPTEST_ASSERT(text.find("\"version\": 1") != std::string::npos);
    auto decoded = deserialize_profiles(text);
    CPPTEST_ASSERT(std::holds_alternative<ProfileCollection>(decoded));
    const auto& profiles = std::get<ProfileCollection>(decoded);
    CPPTEST_ASSERT(*profiles.default_name() == "Bollinger");
    const auto* profile = profiles.find("Bollinger");
    CPPTEST_ASSERT(profile && profile->layers[0].style.band_fill_opacity == 0.25F);
    CPPTEST_ASSERT(std::get<std::int64_t>(profile->indicators[0].parameters.at("period")) == 20);
    CPPTEST_ASSERT(profile->indicators[0].name == "Wide bands");
    auto invalid = deserialize_profiles("{\"version\":2,\"profiles\":[],\"defaultProfile\":null}");
    CPPTEST_ASSERT(std::holds_alternative<ProfileError>(invalid));
    return 0;
}

int test_profile_lifecycle() {
    ProfileCollection profiles;
    CPPTEST_ASSERT(!profiles.add(bollinger_profile()) && !profiles.set_default("Bollinger"));
    CPPTEST_ASSERT(!profiles.rename("Bollinger", "Bands") && *profiles.default_name() == "Bands");

    auto replacement = bollinger_profile();
    replacement.indicators[0].parameters["period"] = std::int64_t{30};
    CPPTEST_ASSERT(!profiles.update("Bands", replacement));
    CPPTEST_ASSERT(std::get<std::int64_t>(profiles.find("Bands")->indicators[0].parameters.at("period")) == 30);
    CPPTEST_ASSERT(profiles.remove("Bands") && profiles.profiles().empty() && !profiles.default_name());
    return 0;
}

int test_capture_and_atomic_replacement() {
    auto source = chart("source");
    const auto first = source.add_indicator("sma", {{"period", std::int64_t{50}}});
    const auto second = source.add_indicator("sma", {{"period", std::int64_t{20}}});
    source.set_indicator_name(first, "Duplicate");
    source.set_indicator_name(second, "Duplicate");
    source.set_indicator_enabled(second, false);
    CPPTEST_ASSERT(source.move_indicator(second, 0));
    source.add_layer(LayerKind::Line, {{second, "value"}}, {{0.1F, 0.2F, 0.3F, 0.4F}, false, 3.0F, 0.7F},
                     LayerPane::Separate);

    const auto captured = capture_profile(source, "Actual");
    CPPTEST_ASSERT(captured.indicators[0].parameters.at("period") ==
                   Didrachma::Analysis::Core::Indicator::ParameterValue{std::int64_t{20}});
    CPPTEST_ASSERT(!captured.indicators[0].enabled && captured.indicators[0].name == "Duplicate");
    CPPTEST_ASSERT(!captured.layers[0].style.visible && captured.layers[0].style.line_width == 3.0F);
    CPPTEST_ASSERT(captured.layers[0].pane == LayerPane::Separate);

    auto target = chart("target");
    target.add_indicator("ema", {{"period", std::int64_t{9}}});
    CPPTEST_ASSERT(!apply_profile(captured, target));
    CPPTEST_ASSERT(target.indicators().size() == 2 && target.layers().size() == 1);
    CPPTEST_ASSERT(target.layers()[0].pane == LayerPane::Separate);
    CPPTEST_ASSERT(target.indicators()[0].instance.id != captured.indicators[0].id);
    const auto before = capture_profile(target, "Before");
    auto invalid = captured;
    invalid.layers[0].outputs[0].instance_id = "missing";
    CPPTEST_ASSERT(apply_profile(invalid, target).has_value());
    CPPTEST_ASSERT(capture_profile(target, "Before").indicators[0].id == before.indicators[0].id);
    return 0;
}

int test_pattern_instance_and_marker_round_trip() {
    auto source = chart("patterns");
    const auto pattern = source.add_indicator("cdldarkcloudcover", {{"penetration", 0.35}});
    source.set_indicator_name(pattern, "Cloud 35%");
    source.set_indicator_enabled(pattern, false);
    source.add_layer(LayerKind::Marker, {{pattern, "value"}}, {{1.0F, 0.78F, 0.18F, 1.0F}, true, 2.0F, 0.2F});
    ProfileCollection profiles;
    CPPTEST_ASSERT(!profiles.add(capture_profile(source, "Patterns")));
    const auto decoded = deserialize_profiles(serialize_profiles(profiles));
    CPPTEST_ASSERT(std::holds_alternative<ProfileCollection>(decoded));
    auto target = chart("restored");
    CPPTEST_ASSERT(!apply_profile(*std::get<ProfileCollection>(decoded).find("Patterns"), target));
    CPPTEST_ASSERT(target.indicators().size() == 1 && !target.indicators()[0].instance.enabled);
    CPPTEST_ASSERT(target.indicators()[0].name == "Cloud 35%");
    CPPTEST_ASSERT(std::get<double>(target.indicators()[0].instance.parameters.at("penetration")) == 0.35);
    CPPTEST_ASSERT(target.layers().size() == 1 && target.layers()[0].kind == LayerKind::Marker);
    return 0;
}

int main() {
    CPPTEST_RUN(test_default_validation_and_copy_on_apply);
    CPPTEST_RUN(test_versioned_serialization_round_trip_and_rejection);
    CPPTEST_RUN(test_profile_lifecycle);
    CPPTEST_RUN(test_capture_and_atomic_replacement);
    CPPTEST_RUN(test_pattern_instance_and_marker_round_trip);
    return 0;
}
