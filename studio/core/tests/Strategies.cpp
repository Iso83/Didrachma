#include "TestAssert.h"

#include <Didrachma/analysis/adapters/talib/Analyzer.h>
#include <Didrachma/studio/core/Strategies.h>
#include <filesystem>

using namespace Didrachma;

namespace {
Strategy::Core::Definition definition(const Analysis::Core::Indicator::Definition& indicator) {
    Strategy::Core::Definition value;
    value.id = "monitor-test";
    value.display_name = "Monitor test";
    value.primary_series_id = "primary";
    value.series.push_back({"primary",
                            "fixture",
                            {Strategy::Core::InstrumentKind::Subject, {}},
                            {10, Market::Core::Time::Unit::Minute},
                            {}});
    std::map<std::string, Analysis::Core::Indicator::ParameterValue> parameters;
    for (const auto& parameter : indicator.parameters)
        parameters.emplace(parameter.id, parameter.default_value);
    value.indicators.push_back({"required-study", "primary", indicator.id, std::move(parameters)});
    value.entry.condition = std::make_shared<Strategy::Core::ConditionExpression>();
    value.entry.condition->id = "entry-leaf";
    value.entry.condition->kind = Strategy::Core::ConditionKind::IndicatorComparison;
    value.entry.condition->predicate = Strategy::Core::IndicatorComparison{
        "required-study", indicator.outputs.front().id, Strategy::Core::Comparison::Greater, 0.0};
    value.entry.order = {Strategy::Core::EntryOrderKind::NextBarOpen};
    value.stop_loss = {Strategy::Core::PricePolicyKind::PercentageFromEntry, 2.0};
    value.target = {Strategy::Core::PricePolicyKind::PercentageFromEntry, 4.0};
    return value;
}
} // namespace

int main() {
    Analysis::Adapters::TaLib::Analyzer analyzer;
    const auto catalog = analyzer.catalog();
    const auto usable = std::ranges::find_if(catalog, [](const auto& item) { return !item.outputs.empty(); });
    CPPTEST_ASSERT(usable != catalog.end());

    Studio::Core::Strategies strategies{analyzer};
    auto& created = strategies.create(definition(*usable));
    CPPTEST_ASSERT(created.unsaved);
    const auto created_id = created.definition.id;
    auto& duplicate = strategies.duplicate(created_id);
    CPPTEST_ASSERT(duplicate.definition.id != created_id);
    CPPTEST_ASSERT(!strategies.erase(duplicate.definition.id));
    CPPTEST_ASSERT(strategies.erase(duplicate.definition.id, true));

    const auto path = std::filesystem::temp_directory_path() / "didrachma-studio-strategy-test.json";
    CPPTEST_ASSERT(strategies.save(created_id, path, catalog).empty());
    Studio::Core::Strategies imported{analyzer};
    CPPTEST_ASSERT(imported.import(path, catalog).empty());
    CPPTEST_ASSERT(imported.definitions().size() == 1);

    std::vector<StockChart::Core::Document> charts;
    const auto open_chart = [&](const Market::Core::Series::Key& key) -> StockChart::Core::Document& {
        const auto existing = std::ranges::find(charts, key, &StockChart::Core::Document::series);
        if (existing != charts.end())
            return *existing;

        charts.emplace_back("chart-" + std::to_string(charts.size() + 1), key,
                            Market::Core::Time::Range{Market::Core::Time::UtcTimestamp{},
                                                      Market::Core::Time::UtcTimestamp{std::chrono::hours{24}}});
        return charts.back();
    };
    auto* run = imported.start("monitor-test", "SUBJECT", catalog, open_chart);
    CPPTEST_ASSERT(run && run->series.size() == 1 && charts.size() == 1);
    const auto first_run_id = run->id;
    CPPTEST_ASSERT(charts.front().indicators().size() == 1);
    const auto manual = charts.front().add_indicator(usable->id, {});
    auto* second = imported.start("monitor-test", "SUBJECT", catalog, open_chart);
    const auto second_run_id = second ? second->id : std::string{};
    CPPTEST_ASSERT(second && charts.size() == 1 && charts.front().indicators().size() == 2);
    auto* other_subject = imported.start("monitor-test", "OTHER", catalog, open_chart);
    const auto other_run_id = other_subject ? other_subject->id : std::string{};
    CPPTEST_ASSERT(other_subject && charts.size() == 2 && charts.back().indicators().size() == 1);
    const auto timestamp = [](std::int64_t seconds) {
        return Market::Core::Time::UtcTimestamp{std::chrono::seconds{seconds}};
    };
    std::vector<Market::Core::Series::Bar> closed_bars{
        {timestamp(60), timestamp(120), 100, 102, 99, 101, 1000, Market::Core::Series::BarState::Closed},
        {timestamp(120), timestamp(180), 101, 104, 100, 103, 1100, Market::Core::Series::BarState::Closed}};
    imported.update([&](std::string_view) { return std::span<const Market::Core::Series::Bar>{closed_bars}; });
    const auto session_path = std::filesystem::temp_directory_path() / "didrachma-studio-session-test.json";
    CPPTEST_ASSERT(!imported.save_session(session_path));
    CPPTEST_ASSERT(imported.stop(first_run_id, {}, [&](std::string_view) { return &charts.front(); }));
    CPPTEST_ASSERT(!imported.close_view(charts.front().id()));
    CPPTEST_ASSERT(std::ranges::find(imported.runs(), second_run_id, &Studio::Core::StrategyRunMonitor::id)->state ==
                   Strategy::Core::RunState::WaitingForEntry);
    CPPTEST_ASSERT(imported.stop(second_run_id, {}, [&](std::string_view) { return &charts.front(); }));
    CPPTEST_ASSERT(charts.front().find_indicator(manual));
    CPPTEST_ASSERT(charts.front().indicators().size() == 1);
    CPPTEST_ASSERT(imported.stop(other_run_id, {}, [&](std::string_view id) {
        const auto found = std::ranges::find(charts, id, &StockChart::Core::Document::id);
        return found == charts.end() ? nullptr : &*found;
    }));
    CPPTEST_ASSERT(charts.back().indicators().empty());
    CPPTEST_ASSERT(imported.close_view(charts.back().id()));
    Studio::Core::Strategies restored{analyzer};
    CPPTEST_ASSERT(!restored.restore_session(session_path, catalog));
    CPPTEST_ASSERT(restored.definitions().size() == 1);
    CPPTEST_ASSERT(restored.runs().size() == 3 && restored.runs().front().restored_summary);
    std::filesystem::remove(session_path);
    std::filesystem::remove(path);
}
