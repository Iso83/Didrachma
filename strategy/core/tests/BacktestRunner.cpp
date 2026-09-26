#include "TestAssert.h"

#include <Didrachma/strategy/core/Backtest.h>
#include <array>
#include <cmath>

using namespace Didrachma;
using namespace Didrachma::Strategy::Core;
using namespace std::chrono_literals;

namespace {
using Bar = Market::Core::Series::Bar;
using Timestamp = Market::Core::Time::UtcTimestamp;

class Analyzer final : public Analysis::Core::Indicator::Analyzer {
public:
    std::vector<Analysis::Core::Indicator::Definition> catalog() const override {
        return {};
    }
    Analysis::Core::Indicator::CalculationOutcome
    calculate(const Analysis::Core::Indicator::CalculationRequest&) override {
        return {};
    }
};

class LateInsufficientAnalyzer final : public Analysis::Core::Indicator::Analyzer {
public:
    std::vector<Analysis::Core::Indicator::Definition> catalog() const override {
        return {{"late", "Late", {}, {{"value", "Value"}}}};
    }
    std::size_t required_history(const Analysis::Core::Indicator::Instance&) const override {
        return 1;
    }
    Analysis::Core::Indicator::CalculationOutcome
    calculate(const Analysis::Core::Indicator::CalculationRequest& request) override {
        Analysis::Core::Indicator::Result result;
        result.input_revision = request.input_revision;
        result.state = Analysis::Core::Indicator::CalculationState::InsufficientHistory;
        result.required_history = 9;
        return {result, Analysis::Core::Indicator::RecalculationKind::Full};
    }
};

class DerivedAnalyzer final : public Analysis::Core::Indicator::Analyzer {
public:
    std::size_t calculations{};

    std::vector<Analysis::Core::Indicator::Definition> catalog() const override {
        return {{"derived", "Derived", {}, {{"value", "Value"}}}};
    }
    std::size_t required_history(const Analysis::Core::Indicator::Instance&) const override {
        return 3;
    }
    Analysis::Core::Indicator::CalculationOutcome
    calculate(const Analysis::Core::Indicator::CalculationRequest& request) override {
        ++calculations;
        Analysis::Core::Indicator::Result result;
        result.input_revision = request.input_revision;
        result.state = request.bars.size() < 3 ? Analysis::Core::Indicator::CalculationState::InsufficientHistory
                                               : Analysis::Core::Indicator::CalculationState::Ready;
        result.required_history = 3;
        Analysis::Core::Indicator::OutputSeries output{"value"};
        for (const auto& item : request.bars)
            output.samples.push_back({item.open_time, item.close});
        result.outputs.push_back(std::move(output));
        return {result, Analysis::Core::Indicator::RecalculationKind::Full, 0, request.bars.size(), 0};
    }
};

class Provider final : public Market::Core::Provider::Data {
public:
    std::vector<Bar> bars;
    std::optional<std::string> failure;
    std::size_t loads{};
    std::vector<Market::Core::Provider::HistoryRequest> requests;

    Market::Core::Provider::CapabilitySet capabilities() const override {
        return Market::Core::Provider::CapabilitySet::from(Market::Core::Provider::Capability::History);
    }
    Market::Core::Provider::HistoryResult load_history(const Market::Core::Provider::HistoryRequest& request) override {
        ++loads;
        requests.push_back(request);
        if (failure)
            return Market::Core::Provider::Error{*failure};
        std::vector<Bar> selected;
        for (const auto& bar : bars)
            if (bar.close_time && *bar.close_time >= request.range.begin && *bar.close_time < request.range.end)
                selected.push_back(bar);
        return selected;
    }
    std::unique_ptr<Market::Core::Provider::Subscription> subscribe(const Market::Core::Series::Key&,
                                                                    Market::Core::Provider::UpdateHandler) override {
        return {};
    }
};

Timestamp at(std::int64_t seconds) {
    return Timestamp{std::chrono::seconds{seconds}};
}

Bar bar(std::int64_t close_seconds, double open, double high, double low, double close) {
    return {at(close_seconds - 600),
            at(close_seconds),
            open,
            high,
            low,
            close,
            1000,
            Market::Core::Series::BarState::Closed};
}

std::shared_ptr<ConditionExpression> condition(double threshold = 100) {
    auto result = std::make_shared<ConditionExpression>();
    result->id = "entry";
    result->kind = ConditionKind::MarketComparison;
    result->predicate = MarketComparison{"primary", MarketField::Close, Comparison::Greater, threshold};
    return result;
}

Definition definition() {
    Definition result;
    result.id = "runner";
    result.display_name = "Runner fixture";
    result.primary_series_id = "primary";
    result.series = {{"primary", "fixture", {InstrumentKind::Subject, {}}, {10, Market::Core::Time::Unit::Minute}, {}}};
    result.entry.condition = condition();
    result.entry.order = {EntryOrderKind::NextBarOpen};
    result.stop_loss = {PricePolicyKind::Absolute, 95};
    result.target = {PricePolicyKind::Absolute, 110};
    return result;
}

BacktestRequest request() {
    return {definition(), "ACME", at(1200), at(3000), {"fixture", {}}, {2, 1000, 1, 0.5, 0}, 1};
}

bool near(double left, double right) {
    return std::abs(left - right) < 0.000001;
}

std::shared_ptr<ConditionExpression> sequence(std::shared_ptr<ConditionExpression> leaf,
                                              std::optional<Duration> elapsed,
                                              std::optional<std::uint64_t> closed_bars) {
    auto result = std::make_shared<ConditionExpression>();
    result->id = "sequence";
    result->kind = ConditionKind::Sequence;
    result->sequence = {elapsed, closed_bars};
    result->children = {std::move(leaf)};
    return result;
}

BacktestRequest derived_primary_request(std::optional<Duration> elapsed, std::optional<std::uint64_t> closed_bars) {
    auto value = request();
    value.from = at(0);
    value.through = at(7200);
    value.strategy_snapshot.series = {
        {"primary", "fixture", {InstrumentKind::Subject, {}}, {1, Market::Core::Time::Unit::Hour}, {}},
        {"native", "fixture", {InstrumentKind::Subject, {}}, {10, Market::Core::Time::Unit::Minute}, {}}};
    value.strategy_snapshot.indicators = {{"primary-value", "primary", "derived", {}}};
    auto leaf = std::make_shared<ConditionExpression>();
    leaf->id = "primary-leaf";
    leaf->kind = ConditionKind::IndicatorComparison;
    leaf->predicate = IndicatorComparison{"primary-value", "value", Comparison::Greater, 1000};
    value.strategy_snapshot.entry.condition = sequence(std::move(leaf), elapsed, closed_bars);
    return value;
}

void add_history(Provider& provider, std::int64_t first_close, std::int64_t last_close) {
    for (auto close = first_close; close <= last_close; close += 600)
        provider.bars.push_back(bar(close, 100, 102, 99, 101));
}
} // namespace

int test_nonzero_duration_trade_and_inclusive_range() {
    Analyzer analyzer;
    Provider provider;
    provider.bars = {bar(600, 98, 100, 97, 99),     bar(1200, 99, 103, 98, 101),   bar(1800, 102, 106, 101, 104),
                     bar(2400, 104, 109, 103, 108), bar(3000, 108, 111, 107, 110), bar(3600, 200, 220, 190, 210)};
    const std::array<Market::Core::Time::Frame, 1> supported{
        Market::Core::Time::Frame{10, Market::Core::Time::Unit::Minute}};
    const auto outcome = BacktestRunner{analyzer}.run(request(), provider, supported);
    CPPTEST_ASSERT(outcome.status == BacktestStatus::Completed &&
                   outcome.result_status == BacktestResultStatus::Exited);
    CPPTEST_ASSERT(outcome.status_history ==
                   std::vector<BacktestStatus>({BacktestStatus::Preparing, BacktestStatus::Ready,
                                                BacktestStatus::Executing, BacktestStatus::Completed}));
    CPPTEST_ASSERT(outcome.warmup_begin == at(0));
    CPPTEST_ASSERT(outcome.result->entry_time == at(1200) && outcome.result->exit_time == at(2400));
    CPPTEST_ASSERT(near(*outcome.result->entry_price, 102) && near(*outcome.result->exit_price, 110));
    CPPTEST_ASSERT(near(outcome.result->projection.segments[1].price, 95) &&
                   near(outcome.result->projection.segments[2].price, 110));
    CPPTEST_ASSERT(outcome.result->duration == 1200s && near(outcome.result->gross_profit_loss, 16));
    CPPTEST_ASSERT(near(outcome.result->net_profit_loss, 11.88));
    CPPTEST_ASSERT(outcome.inputs.front().state.readiness == Readiness::Ready);
    return 0;
}

int test_result_statuses_are_distinct() {
    Analyzer analyzer;
    Provider provider;
    const std::array<Market::Core::Time::Frame, 1> supported{
        Market::Core::Time::Frame{10, Market::Core::Time::Unit::Minute}};
    provider.bars = {bar(1200, 98, 99, 97, 98), bar(1800, 98, 99, 97, 98)};
    auto value = request();
    value.through = at(1800);
    auto outcome = BacktestRunner{analyzer}.run(value, provider, supported);
    CPPTEST_ASSERT(outcome.result_status == BacktestResultStatus::NoSignal);

    provider.bars = {bar(1200, 99, 102, 98, 101)};
    value.through = at(1200);
    outcome = BacktestRunner{analyzer}.run(value, provider, supported);
    CPPTEST_ASSERT(outcome.result_status == BacktestResultStatus::SignalNotFilled);

    provider.bars = {bar(1200, 99, 102, 98, 101), bar(1800, 102, 106, 101, 104)};
    value.through = at(1800);
    outcome = BacktestRunner{analyzer}.run(value, provider, supported);
    CPPTEST_ASSERT(outcome.result_status == BacktestResultStatus::OpenPositionClosedAtEnd);
    return 0;
}

int test_structured_preparation_errors_and_capability_preflight() {
    Analyzer analyzer;
    Provider provider;
    const std::array<Market::Core::Time::Frame, 1> supported{
        Market::Core::Time::Frame{10, Market::Core::Time::Unit::Minute}};
    auto value = request();
    value.from = at(3001);
    auto outcome = BacktestRunner{analyzer}.run(value, provider, supported);
    CPPTEST_ASSERT(outcome.errors.front().code == BacktestErrorCode::InvalidRequest && provider.loads == 0);

    value = request();
    value.strategy_snapshot.series.front().timeframe.quantity = 2000;
    outcome = BacktestRunner{analyzer}.run(value, provider, supported);
    CPPTEST_ASSERT(outcome.errors.front().code == BacktestErrorCode::UnsupportedTimeframe && provider.loads == 0);

    value = request();
    provider.failure = "offline";
    outcome = BacktestRunner{analyzer}.run(value, provider, supported);
    CPPTEST_ASSERT(outcome.errors.front().code == BacktestErrorCode::ProviderFailure);

    provider.failure.reset();
    provider.bars.clear();
    outcome = BacktestRunner{analyzer}.run(value, provider, supported);
    CPPTEST_ASSERT(outcome.errors.front().code == BacktestErrorCode::InsufficientWarmup);
    return 0;
}

int test_no_bars_in_range_and_cancellation_are_structured() {
    Analyzer analyzer;
    Provider provider;
    provider.bars = {bar(600, 99, 100, 98, 99)};
    const std::array<Market::Core::Time::Frame, 1> supported{
        Market::Core::Time::Frame{10, Market::Core::Time::Unit::Minute}};
    const auto no_bars = BacktestRunner{analyzer}.run(request(), provider, supported);
    CPPTEST_ASSERT(no_bars.errors.front().code == BacktestErrorCode::NoBarsInRange);
    const auto cancelled = BacktestRunner{analyzer}.run(request(), provider, supported, [] { return true; });
    CPPTEST_ASSERT(cancelled.status == BacktestStatus::Cancelled &&
                   cancelled.errors.front().code == BacktestErrorCode::Cancelled);
    return 0;
}

int test_analyzer_insufficient_history_overrides_met_plan_and_loads_are_grouped() {
    LateInsufficientAnalyzer analyzer;
    Provider provider;
    provider.bars = {bar(600, 98, 100, 97, 99), bar(1200, 99, 103, 98, 101), bar(1800, 102, 106, 101, 104),
                     bar(2400, 104, 109, 103, 108), bar(3000, 108, 111, 107, 110)};
    auto value = request();
    value.strategy_snapshot.series.push_back(value.strategy_snapshot.series.front());
    value.strategy_snapshot.series.back().id = "primary-copy";
    value.strategy_snapshot.indicators = {{"late-binding", "primary-copy", "late", {}}};
    value.strategy_snapshot.entry.condition = std::make_shared<ConditionExpression>();
    value.strategy_snapshot.entry.condition->id = "late-condition";
    value.strategy_snapshot.entry.condition->kind = ConditionKind::IndicatorComparison;
    value.strategy_snapshot.entry.condition->predicate =
        IndicatorComparison{"late-binding", "value", Comparison::Greater, 0};
    const std::array<Market::Core::Time::Frame, 1> supported{
        Market::Core::Time::Frame{10, Market::Core::Time::Unit::Minute}};
    const auto outcome = BacktestRunner{analyzer}.run(value, provider, supported);
    CPPTEST_ASSERT(provider.loads == 1);
    CPPTEST_ASSERT(outcome.status == BacktestStatus::Failed);
    CPPTEST_ASSERT(outcome.errors.front().code == BacktestErrorCode::InsufficientWarmup);
    CPPTEST_ASSERT(outcome.inputs.back().state.readiness == Readiness::InsufficientHistory);
    CPPTEST_ASSERT(outcome.inputs.back().state.required_history == 9);
    return 0;
}

int test_derived_target_uses_one_native_source_load_with_converted_warmup() {
    DerivedAnalyzer analyzer;
    Provider provider;
    for (std::int64_t close = -10800; close <= 3600; close += 600)
        provider.bars.push_back(bar(close, 100, 102, 99, 101));
    auto value = request();
    value.strategy_snapshot.series.push_back(
        {"hourly", "fixture", {InstrumentKind::Subject, {}}, {1, Market::Core::Time::Unit::Hour}, {}});
    value.strategy_snapshot.indicators = {{"derived-binding", "hourly", "derived", {}}};
    value.strategy_snapshot.entry.condition = std::make_shared<ConditionExpression>();
    value.strategy_snapshot.entry.condition->id = "derived-condition";
    value.strategy_snapshot.entry.condition->kind = ConditionKind::IndicatorComparison;
    value.strategy_snapshot.entry.condition->predicate =
        IndicatorComparison{"derived-binding", "value", Comparison::Greater, 200};
    const std::array<Market::Core::Time::Frame, 1> supported{
        Market::Core::Time::Frame{10, Market::Core::Time::Unit::Minute}};
    const auto outcome = BacktestRunner{analyzer}.run(value, provider, supported);
    CPPTEST_ASSERT(outcome.status == BacktestStatus::Completed);
    CPPTEST_ASSERT(provider.loads == 1);
    CPPTEST_ASSERT(analyzer.calculations == 1);
    CPPTEST_ASSERT(outcome.inputs.size() == 2 && outcome.inputs[1].series.derived);
    CPPTEST_ASSERT(outcome.inputs[1].series.source_binding_id == "primary");
    CPPTEST_ASSERT(outcome.warmup_begin && *outcome.warmup_begin <= at(-9600));
    return 0;
}

int test_derived_primary_closed_bar_sequence_plans_and_loads_native_warmup() {
    DerivedAnalyzer analyzer;
    Provider provider;
    add_history(provider, -36000, 7200);
    const std::array supported{Market::Core::Time::Frame{10, Market::Core::Time::Unit::Minute}};

    const auto outcome = BacktestRunner{analyzer}.run(derived_primary_request({}, 6), provider, supported);

    CPPTEST_ASSERT(outcome.status == BacktestStatus::Completed && outcome.errors.empty());
    CPPTEST_ASSERT(provider.loads == 1 && analyzer.calculations == 1);
    CPPTEST_ASSERT(provider.requests.size() == 1 && provider.requests.front().range.begin == at(-57600) &&
                   provider.requests.front().range.end == at(7201));
    CPPTEST_ASSERT(outcome.warmup_begin == at(-36600));
    CPPTEST_ASSERT(outcome.inputs.size() == 2);
    CPPTEST_ASSERT(outcome.inputs[0].series.derived && outcome.inputs[0].state.readiness == Readiness::Ready &&
                   outcome.inputs[0].state.required_history == 8);
    CPPTEST_ASSERT(!outcome.inputs[1].series.derived && outcome.inputs[1].state.readiness == Readiness::Ready &&
                   outcome.inputs[1].state.required_history == 48 && outcome.inputs[1].state.available_history == 73);
    return 0;
}

int test_derived_primary_elapsed_sequence_plans_native_warmup_independently() {
    DerivedAnalyzer analyzer;
    Provider provider;
    add_history(provider, -36000, 7200);
    const std::array supported{Market::Core::Time::Frame{10, Market::Core::Time::Unit::Minute}};

    const auto outcome = BacktestRunner{analyzer}.run(derived_primary_request(6h, {}), provider, supported);

    CPPTEST_ASSERT(outcome.status == BacktestStatus::Completed && provider.loads == 1 && analyzer.calculations == 1);
    CPPTEST_ASSERT(provider.requests.front().range.begin == at(-57600));
    CPPTEST_ASSERT(outcome.inputs[0].state.required_history == 8 &&
                   outcome.inputs[0].state.readiness == Readiness::Ready);
    CPPTEST_ASSERT(outcome.inputs[1].state.required_history == 48 &&
                   outcome.inputs[1].state.readiness == Readiness::Ready);
    return 0;
}

int test_sequence_dependency_plans_its_own_analyzer_warmup_and_reuses_each_key() {
    DerivedAnalyzer analyzer;
    Provider provider;
    add_history(provider, -48000, 7200);
    auto value = derived_primary_request({}, 6);
    value.strategy_snapshot.series.push_back(
        {"context", "fixture", {InstrumentKind::Fixed, "PEER"}, {10, Market::Core::Time::Unit::Minute}, {}});
    value.strategy_snapshot.indicators.push_back({"context-value", "context", "derived", {}});
    auto leaf = std::make_shared<ConditionExpression>();
    leaf->id = "context-leaf";
    leaf->kind = ConditionKind::IndicatorComparison;
    leaf->predicate = IndicatorComparison{"context-value", "value", Comparison::Greater, 1000};
    value.strategy_snapshot.entry.condition = sequence(std::move(leaf), {}, 6);
    const std::array supported{Market::Core::Time::Frame{10, Market::Core::Time::Unit::Minute}};

    const auto outcome = BacktestRunner{analyzer}.run(value, provider, supported);

    CPPTEST_ASSERT(outcome.status == BacktestStatus::Completed && outcome.inputs.size() == 3);
    CPPTEST_ASSERT(provider.loads == 2 && provider.requests.size() == 2 && analyzer.calculations == 2);
    const auto context_request =
        std::ranges::find(provider.requests, "PEER", [](const auto& request) { return request.key.instrument; });
    CPPTEST_ASSERT(context_request != provider.requests.end() && context_request->range.begin == at(-45600));
    CPPTEST_ASSERT(outcome.inputs[2].state.required_history == 38 && outcome.inputs[2].state.available_history == 89 &&
                   outcome.inputs[2].state.readiness == Readiness::Ready);
    return 0;
}

int test_derived_primary_reports_insufficient_real_provider_history() {
    DerivedAnalyzer analyzer;
    Provider provider;
    add_history(provider, -6000, 7200);
    const std::array supported{Market::Core::Time::Frame{10, Market::Core::Time::Unit::Minute}};

    const auto outcome = BacktestRunner{analyzer}.run(derived_primary_request({}, 6), provider, supported);

    CPPTEST_ASSERT(outcome.status == BacktestStatus::Failed && outcome.result_status == BacktestResultStatus::Failed);
    CPPTEST_ASSERT(outcome.errors.size() == 1 && outcome.errors.front().code == BacktestErrorCode::InsufficientWarmup);
    CPPTEST_ASSERT(provider.loads == 5 && analyzer.calculations == 0);
    CPPTEST_ASSERT(provider.requests.front().range.begin == at(-57600) &&
                   provider.requests.back().range.begin == at(-921600));
    return 0;
}

int main() {
    CPPTEST_RUN(test_nonzero_duration_trade_and_inclusive_range);
    CPPTEST_RUN(test_result_statuses_are_distinct);
    CPPTEST_RUN(test_structured_preparation_errors_and_capability_preflight);
    CPPTEST_RUN(test_no_bars_in_range_and_cancellation_are_structured);
    CPPTEST_RUN(test_analyzer_insufficient_history_overrides_met_plan_and_loads_are_grouped);
    CPPTEST_RUN(test_derived_target_uses_one_native_source_load_with_converted_warmup);
    CPPTEST_RUN(test_derived_primary_closed_bar_sequence_plans_and_loads_native_warmup);
    CPPTEST_RUN(test_derived_primary_elapsed_sequence_plans_native_warmup_independently);
    CPPTEST_RUN(test_sequence_dependency_plans_its_own_analyzer_warmup_and_reuses_each_key);
    CPPTEST_RUN(test_derived_primary_reports_insufficient_real_provider_history);
    return 0;
}
