#include "TestAssert.h"

#include <Didrachma/studio/core/BacktestSetup.h>
#include <Didrachma/studio/core/Strategies.h>
#include <Didrachma/studio/core/StrategyEditor.h>
#include <cmath>
#include <filesystem>

using namespace Didrachma;

namespace {
class Analyzer final : public Analysis::Core::Indicator::Analyzer {
public:
    std::vector<Analysis::Core::Indicator::Definition> catalog() const override {
        return {};
    }
    std::size_t required_history(const Analysis::Core::Indicator::Instance&) const override {
        return 3;
    }
    Analysis::Core::Indicator::CalculationOutcome
    calculate(const Analysis::Core::Indicator::CalculationRequest&) override {
        return Analysis::Core::Indicator::CalculationOutcome{};
    }
};

Strategy::Core::Definition definition() {
    Strategy::Core::Definition value;
    value.id = "setup";
    value.display_name = "Subject and peer";
    value.version = 7;
    value.primary_series_id = "subject-10m";
    value.series = {{"subject-10m",
                     "yahoo",
                     {Strategy::Core::InstrumentKind::Subject, {}},
                     {10, Market::Core::Time::Unit::Minute},
                     {},
                     "Subject 10 minutes"},
                    {"peer",
                     "yahoo",
                     {Strategy::Core::InstrumentKind::Fixed, "XLK"},
                     {1, Market::Core::Time::Unit::Day},
                     {},
                     "Sector peer"}};
    value.entry.condition = std::make_shared<Strategy::Core::ConditionExpression>();
    value.entry.condition->id = "entry";
    value.entry.condition->kind = Strategy::Core::ConditionKind::MarketComparison;
    value.entry.condition->predicate = Strategy::Core::MarketComparison{
        "subject-10m", Strategy::Core::MarketField::Close, Strategy::Core::Comparison::Greater, 1};
    value.stop_loss = {Strategy::Core::PricePolicyKind::PercentageFromEntry, 2};
    value.target = {Strategy::Core::PricePolicyKind::PercentageFromEntry, 4};
    return value;
}

const std::vector<Strategy::Core::BacktestSourceCapability> capabilities{
    {{10, Market::Core::Time::Unit::Minute}, {5, Market::Core::Time::Unit::Minute}, std::chrono::days{60}},
    {{1, Market::Core::Time::Unit::Day}, {1, Market::Core::Time::Unit::Day}, std::nullopt}};

Market::Core::Time::UtcTimestamp date(int year, unsigned month, unsigned day, int hour = 0, int minute = 0,
                                      int second = 0) {
    return Market::Core::Time::UtcTimestamp{std::chrono::sys_days{
               std::chrono::year{year} / std::chrono::month{month} /
               std::chrono::day{day}}.time_since_epoch()} +
           std::chrono::hours{hour} + std::chrono::minutes{minute} + std::chrono::seconds{second};
}

bool has(const Studio::Core::BacktestSetupValidation& result, std::string_view path) {
    return std::ranges::any_of(result.errors, [&](const auto& error) { return error.path == path; });
}
} // namespace

int main() {
    Analyzer analyzer;
    Studio::Core::Strategies strategies{analyzer};
    auto& record = strategies.create(definition());
    record.unsaved = false;
    const auto runs_before = strategies.runs().size();
    auto setup = strategies.new_backtest("setup", analyzer.catalog());
    CPPTEST_ASSERT(setup && strategies.runs().size() == runs_before);

    auto& values = setup->values();
    values.subject = "AAPL";
    values.from_date = "2026-08-01";
    values.through_date = "2026-08-31";
    values.execution = {12.5, 50000.0, 1.25, .15, .05};
    values.fill_model_version = 1;
    const auto now = date(2026, 9, 28, 12);
    auto valid = setup->validate(analyzer, capabilities, now, false);
    CPPTEST_ASSERT(valid.request && valid.preview.inputs.size() == 2);
    CPPTEST_ASSERT(valid.request->from == date(2026, 8, 1));
    CPPTEST_ASSERT(valid.request->through == date(2026, 8, 31, 23, 59, 59));
    CPPTEST_ASSERT(valid.request->subject_symbol == "AAPL" && valid.request->execution.quantity == 12.5);
    CPPTEST_ASSERT(valid.request->execution.starting_capital == 50000.0 &&
                   valid.request->execution.fixed_per_fill == 1.25 &&
                   valid.request->execution.percentage_per_fill == .15 &&
                   valid.request->execution.slippage_percentage == .05 && valid.request->fill_model_version == 1);
    CPPTEST_ASSERT(valid.preview.inputs[0].resolved_key.instrument == "AAPL" &&
                   valid.preview.inputs[1].resolved_key.instrument == "XLK");
    const Market::Core::Time::Frame five_minutes{5, Market::Core::Time::Unit::Minute};
    CPPTEST_ASSERT(valid.preview.inputs[0].derived && valid.preview.inputs[0].source == five_minutes);
    CPPTEST_ASSERT(!valid.preview.inputs[1].derived && valid.preview.inputs[0].load_begin < valid.request->from);
    CPPTEST_ASSERT(valid.preview.inputs[0].resolved_key.timeframe ==
                   Market::Core::Time::Frame(10, Market::Core::Time::Unit::Minute));
    CPPTEST_ASSERT(valid.preview.inputs[0].provider_request_key.timeframe ==
                   Market::Core::Time::Frame(10, Market::Core::Time::Unit::Minute));
    CPPTEST_ASSERT(valid.preview.inputs[0].load_begin == valid.request->from - std::chrono::minutes{40});
    CPPTEST_ASSERT(valid.preview.inputs[0].exclusive_end == valid.request->through + std::chrono::seconds{1});
    CPPTEST_ASSERT(setup->submit(analyzer, capabilities, now, false) && setup->submission());
    CPPTEST_ASSERT(strategies.runs().size() == runs_before);

    const auto invalid_at = [&](auto mutate, std::string_view path) {
        auto copy = values;
        mutate(copy);
        Studio::Core::BacktestSetup invalid{definition(), copy};
        const auto checked = invalid.validate(analyzer, capabilities, now, false);
        return !checked.request && has(checked, path);
    };
    CPPTEST_ASSERT(invalid_at([](auto& v) { v.subject.clear(); }, "/subjectSymbol"));
    CPPTEST_ASSERT(invalid_at([](auto& v) { v.from_date = "not-a-date"; }, "/from"));
    CPPTEST_ASSERT(invalid_at(
        [](auto& v) {
            v.from_date = "2026-09-02";
            v.through_date = "2026-09-01";
        },
        "/through"));
    CPPTEST_ASSERT(invalid_at([](auto& v) { v.through_date = "2026-10-01"; }, "/through"));
    CPPTEST_ASSERT(invalid_at([](auto& v) { v.execution.quantity = std::nan(""); }, "/execution/quantity"));
    CPPTEST_ASSERT(invalid_at([](auto& v) { v.execution.starting_capital = -1; }, "/execution/startingCapital"));
    CPPTEST_ASSERT(invalid_at([](auto& v) { v.execution.slippage_percentage = -1; }, "/execution"));
    CPPTEST_ASSERT(invalid_at([](auto& v) { v.fill_model_version = 2; }, "/fillModelVersion"));
    const auto runs_before_dirty = strategies.runs().size();
    CPPTEST_ASSERT(!setup->validate(analyzer, capabilities, now, true).request);
    CPPTEST_ASSERT(!setup->submit(analyzer, capabilities, now, true) && !setup->submission());
    CPPTEST_ASSERT(strategies.runs().size() == runs_before_dirty);

    // Applying a draft clears editor dirtiness but leaves the record uncommitted until it is saved.
    Studio::Core::StrategyEditor applied_editor{record.definition};
    applied_editor.set_metadata("Applied definition", record.definition.description, record.definition.direction);
    CPPTEST_ASSERT(applied_editor.dirty());
    CPPTEST_ASSERT(strategies.apply(record.definition.id, applied_editor, analyzer.catalog()).empty());
    CPPTEST_ASSERT(record.unsaved && !applied_editor.dirty());
    const auto runs_before_applied = strategies.runs().size();
    CPPTEST_ASSERT(!setup->validate(analyzer, capabilities, now, record.unsaved).request);
    CPPTEST_ASSERT(!setup->submit(analyzer, capabilities, now, record.unsaved) && !setup->submission());
    CPPTEST_ASSERT(strategies.runs().size() == runs_before_applied);
    CPPTEST_ASSERT(setup->definition().display_name == "Subject and peer");

    const auto saved_path = std::filesystem::temp_directory_path() / "didrachma-applied-strategy.json";
    CPPTEST_ASSERT(strategies.save(record.definition.id, saved_path, analyzer.catalog()).empty());
    CPPTEST_ASSERT(!record.unsaved && setup->definition().display_name == "Subject and peer" &&
                   record.definition.display_name == "Applied definition");
    auto reopened = strategies.new_backtest(record.definition.id, analyzer.catalog(), values);
    CPPTEST_ASSERT(reopened && reopened->definition().display_name == "Applied definition");
    std::filesystem::remove(saved_path);

    auto unsupported = definition();
    unsupported.series[0].timeframe = {2000, Market::Core::Time::Unit::Minute};
    Studio::Core::BacktestSetup impossible{unsupported, values};
    CPPTEST_ASSERT(has(impossible.validate(analyzer, capabilities, now, false), "/strategy/series/0/timeframe"));
    auto derived = definition();
    derived.series[0].timeframe = {1, Market::Core::Time::Unit::Hour};
    derived.series.push_back({"subject-10m-source",
                              "yahoo",
                              {Strategy::Core::InstrumentKind::Subject, {}},
                              {10, Market::Core::Time::Unit::Minute},
                              {},
                              "Subject source"});
    Studio::Core::BacktestSetup derived_setup{derived, values};
    const auto derived_checked = derived_setup.validate(analyzer, capabilities, now, false);
    CPPTEST_ASSERT(derived_checked.request && derived_checked.preview.inputs[0].derived);
    derived.series.pop_back();
    Studio::Core::BacktestSetup missing_source{derived, values};
    CPPTEST_ASSERT(has(missing_source.validate(analyzer, capabilities, now, false), "/strategy/series/0/timeframe"));

    auto fixed = definition();
    fixed.primary_series_id = "peer";
    fixed.series.erase(fixed.series.begin());
    fixed.entry.condition->predicate = Strategy::Core::MarketComparison{"peer", Strategy::Core::MarketField::Close,
                                                                        Strategy::Core::Comparison::Greater, 1};
    Studio::Core::BacktestSetup fixed_setup{fixed, values};
    CPPTEST_ASSERT(fixed_setup.values().subject.empty());
    const auto fixed_checked = fixed_setup.validate(analyzer, capabilities, now, false);
    CPPTEST_ASSERT(fixed_checked.request && !fixed_checked.request->subject_symbol);
    auto old = values;
    old.from_date = "2026-01-01";
    old.through_date = "2026-01-02";
    Studio::Core::BacktestSetup beyond_reach{definition(), old};
    CPPTEST_ASSERT(has(beyond_reach.validate(analyzer, capabilities, now, false), "subject-10m"));

    const auto serialized_before = Strategy::Core::serialize(definition());
    const auto path = std::filesystem::temp_directory_path() / "didrachma-recent-backtest-setup.json";
    Studio::Core::RecentBacktestSetupRepository repository{path};
    CPPTEST_ASSERT(repository.save(values).empty());
    auto loaded = repository.load();
    CPPTEST_ASSERT(std::holds_alternative<Studio::Core::BacktestSetupValues>(loaded));
    auto restored_values = std::get<Studio::Core::BacktestSetupValues>(std::move(loaded));
    CPPTEST_ASSERT(restored_values.subject == values.subject && restored_values.from_date == values.from_date &&
                   restored_values.execution.quantity == values.execution.quantity);
    Studio::Core::BacktestSetup restored{definition(), restored_values};
    CPPTEST_ASSERT(restored.validate(analyzer, capabilities, date(2026, 8, 15), false).request == std::nullopt);
    CPPTEST_ASSERT(Strategy::Core::serialize(definition()) == serialized_before);
    std::filesystem::remove(path);
}
