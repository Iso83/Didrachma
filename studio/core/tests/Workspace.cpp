#include "TestAssert.h"

#include <Didrachma/studio/core/Workspace.h>
#include <filesystem>

using namespace Didrachma;

Market::Core::Time::UtcTimestamp at(int seconds) {
    return Market::Core::Time::UtcTimestamp{std::chrono::seconds{seconds}};
}

StockChart::Core::Profile profile() {
    return {"Trend",
            {{"average", "sma", true, {{"period", std::int64_t{20}}}}},
            {{StockChart::Core::LayerKind::Line,
              {{"average", "value"}},
              {{0.2F, 0.4F, 0.8F, 1.0F}, true, 2.0F, 0.2F},
              StockChart::Core::LayerPane::Separate}}};
}

int test_default_profile_and_chart_isolation() {
    Studio::Core::Workspace workspace;
    CPPTEST_ASSERT(!workspace.profiles().add(profile()));
    CPPTEST_ASSERT(!workspace.profiles().set_default("Trend"));
    Market::Core::Series::Key key{"fake", "ONE", {1, Market::Core::Time::Unit::Day}};
    workspace.create_chart(key, {at(0), at(100)});
    workspace.create_chart({"fake", "TWO", {1, Market::Core::Time::Unit::Day}}, {at(0), at(100)});
    auto& first = workspace.documents()[0];
    CPPTEST_ASSERT(first.id() == "chart-1" && workspace.documents()[1].id() == "chart-2");
    auto changed = first.indicators()[0].instance.parameters;
    changed["period"] = std::int64_t{50};
    CPPTEST_ASSERT(first.set_indicator_parameters(first.indicators()[0].instance.id, changed));
    CPPTEST_ASSERT(std::get<std::int64_t>(workspace.documents()[1].indicators()[0].instance.parameters.at("period")) ==
                   20);
    return 0;
}

int test_workspace_round_trip_and_repository() {
    Studio::Core::Workspace workspace;
    auto configured = profile();
    configured.indicators.push_back({"disabled-average", "ema", false, {{"period", std::int64_t{9}}}, "Disabled EMA"});
    configured.layers.push_back({StockChart::Core::LayerKind::Line,
                                 {{"disabled-average", "value"}},
                                 {{0.8F, 0.3F, 0.2F, 0.7F}, false, 4.0F, 0.1F},
                                 StockChart::Core::LayerPane::Price});
    CPPTEST_ASSERT(!workspace.profiles().add(std::move(configured)));
    CPPTEST_ASSERT(!workspace.profiles().set_default("Trend"));
    auto& chart = workspace.create_chart({"fake", "TEST", {1, Market::Core::Time::Unit::Hour}}, {at(10), at(20)});
    chart.set_auto_follow(false);
    chart.set_price_pane_ratio(0.63F);
    chart.set_separate_pane_ratio(0.61F);
    chart.set_show_hover_values(false);
    CPPTEST_ASSERT(workspace.set_polling(chart.id(), true));
    chart.set_indicator_name(chart.indicators()[0].instance.id, "SMA-20");
    const auto decoded = Studio::Core::deserialize_workspace(Studio::Core::serialize_workspace(workspace));
    CPPTEST_ASSERT(std::holds_alternative<Studio::Core::Workspace>(decoded));
    const auto& restored = std::get<Studio::Core::Workspace>(decoded);
    CPPTEST_ASSERT(restored.documents().size() == 1 && restored.documents()[0].id() == "chart-1");
    CPPTEST_ASSERT(restored.documents()[0].indicators().size() == 2 && !restored.documents()[0].auto_follow());
    CPPTEST_ASSERT(restored.documents()[0].indicators()[0].name == "SMA-20");
    CPPTEST_ASSERT(restored.documents()[0].layers()[0].pane == StockChart::Core::LayerPane::Separate);
    CPPTEST_ASSERT(restored.documents()[0].price_pane_ratio() == 0.63F);
    CPPTEST_ASSERT(restored.documents()[0].separate_pane_ratio() == 0.61F);
    CPPTEST_ASSERT(!restored.documents()[0].show_hover_values());
    CPPTEST_ASSERT(restored.polling("chart-1"));
    CPPTEST_ASSERT(restored.history_request("chart-1"));
    CPPTEST_ASSERT(restored.history_request("chart-1")->range.begin == at(10));
    CPPTEST_ASSERT(restored.history_request("chart-1")->range.end == at(20));

    const auto path = std::filesystem::temp_directory_path() / "didrachma-workspace-test.json";
    Studio::Core::WorkspaceRepository repository(path);
    CPPTEST_ASSERT(!repository.save(workspace));
    auto restarted = repository.load();
    CPPTEST_ASSERT(std::holds_alternative<Studio::Core::Workspace>(restarted));
    auto restored_workspace = std::get<Studio::Core::Workspace>(std::move(restarted));
    CPPTEST_ASSERT(restored_workspace.profiles().default_name() == "Trend");
    const auto* restored_profile = restored_workspace.profiles().find("Trend");
    CPPTEST_ASSERT(restored_profile && restored_profile->indicators.size() == 2 &&
                   restored_profile->layers.size() == 2);
    CPPTEST_ASSERT(restored_profile->indicators[1].definition_id == "ema");
    CPPTEST_ASSERT(!restored_profile->indicators[1].enabled && restored_profile->indicators[1].name == "Disabled EMA");
    CPPTEST_ASSERT(std::get<std::int64_t>(restored_profile->indicators[1].parameters.at("period")) == 9);
    CPPTEST_ASSERT(!restored_profile->layers[1].style.visible && restored_profile->layers[1].style.line_width == 4.0F);

    auto& fresh =
        restored_workspace.create_chart({"fake", "FRESH", {1, Market::Core::Time::Unit::Hour}}, {at(10), at(20)});
    CPPTEST_ASSERT(fresh.indicators().size() == 2 && fresh.layers().size() == 2);
    CPPTEST_ASSERT(fresh.indicators()[1].instance.definition_id == "ema" && !fresh.indicators()[1].instance.enabled);
    CPPTEST_ASSERT(fresh.layers()[1].outputs[0].instance_id == fresh.indicators()[1].instance.id);
    CPPTEST_ASSERT(fresh.layers()[1].style.line_width == 4.0F && !fresh.layers()[1].style.visible);
    std::filesystem::remove(path);
    return 0;
}

int test_version_one_is_rejected_without_guessing_history() {
    const auto decoded = Studio::Core::deserialize_workspace(
        R"({"version":1,"profiles":{"version":1,"profiles":[],"defaultProfile":null},"charts":[],"nextChartId":1,"selectedChartId":null})");
    CPPTEST_ASSERT(std::holds_alternative<Studio::Core::WorkspaceError>(decoded));
    CPPTEST_ASSERT(std::get<Studio::Core::WorkspaceError>(decoded).message.find("no provider history request") !=
                   std::string::npos);
    return 0;
}

int test_close_removes_document_request_and_never_reuses_runtime_identity() {
    Studio::Core::Workspace workspace;
    const auto first =
        workspace.create_chart({"fake", "TEST", {1, Market::Core::Time::Unit::Hour}}, {at(10), at(20)}).id();
    workspace.create_chart({"fake", "OTHER", {1, Market::Core::Time::Unit::Hour}}, {at(10), at(20)});
    workspace.select_chart(first);
    CPPTEST_ASSERT(workspace.set_polling(first, true));
    CPPTEST_ASSERT(workspace.close_chart(first));
    CPPTEST_ASSERT(!workspace.history_request(first));
    CPPTEST_ASSERT(!workspace.polling(first));
    CPPTEST_ASSERT(workspace.documents().size() == 1 && workspace.selected_chart()->id() != first);

    const auto reopened =
        workspace.create_chart({"fake", "TEST", {1, Market::Core::Time::Unit::Hour}}, {at(10), at(20)}).id();
    CPPTEST_ASSERT(reopened != first && workspace.documents().size() == 2);
    CPPTEST_ASSERT(!workspace.close_chart("missing"));
    return 0;
}

int main() {
    CPPTEST_RUN(test_default_profile_and_chart_isolation);
    CPPTEST_RUN(test_workspace_round_trip_and_repository);
    CPPTEST_RUN(test_version_one_is_rejected_without_guessing_history);
    CPPTEST_RUN(test_close_removes_document_request_and_never_reuses_runtime_identity);
    return 0;
}
