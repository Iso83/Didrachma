#include "TestAssert.h"

#include <Didrachma/strategy/core/Evaluation.h>
#include <algorithm>

using namespace Didrachma;
using namespace Didrachma::Strategy::Core;
using namespace std::chrono_literals;

namespace {
using Bar = Market::Core::Series::Bar;
using Timestamp = Market::Core::Time::UtcTimestamp;

class Analyzer final : public Analysis::Core::Indicator::Analyzer {
public:
    std::size_t calculations{};
    std::vector<std::optional<Market::Core::Time::Range>> dirty_ranges;

    std::vector<Analysis::Core::Indicator::Definition> catalog() const override {
        using namespace Analysis::Core::Indicator;
        return {{"identity", "Identity", {}, {{"value", "Value"}}},
                {"pattern",
                 "Pattern",
                 {},
                 {{"value", "Value", VisualKind::Marker}},
                 {},
                 {},
                 PaneHint::PriceOverlay,
                 RangeHint::Derived,
                 {},
                 {},
                 Capability::AnalysisEvent}};
    }

    std::size_t required_history(const Analysis::Core::Indicator::Instance& instance) const override {
        const auto found = instance.parameters.find("period");
        return found == instance.parameters.end() ? 1 : static_cast<std::size_t>(std::get<std::int64_t>(found->second));
    }

    Analysis::Core::Indicator::CalculationOutcome
    calculate(const Analysis::Core::Indicator::CalculationRequest& request) override {
        ++calculations;
        dirty_ranges.push_back(request.dirty_range);
        Analysis::Core::Indicator::Result result;
        result.input_revision = request.input_revision;
        result.revision = request.input_revision;
        result.state = request.bars.empty() ? Analysis::Core::Indicator::CalculationState::InsufficientHistory
                                            : Analysis::Core::Indicator::CalculationState::Ready;
        Analysis::Core::Indicator::OutputSeries output{"value"};
        for (const auto& bar : request.bars)
            output.samples.push_back({bar.open_time, request.instance.definition_id == "pattern"
                                                         ? (bar.close > bar.open ? 100.0 : 0.0)
                                                         : bar.close});
        result.outputs.push_back(std::move(output));
        return {std::move(result), Analysis::Core::Indicator::RecalculationKind::Full};
    }
};

class FakeProvider final : public Market::Core::Provider::Data {
    struct NoSubscription final : Market::Core::Provider::Subscription {};

public:
    std::vector<Bar> history;
    std::size_t loads{};

    Market::Core::Provider::CapabilitySet capabilities() const override {
        return Market::Core::Provider::CapabilitySet::from(Market::Core::Provider::Capability::History);
    }
    Market::Core::Provider::HistoryResult load_history(const Market::Core::Provider::HistoryRequest&) override {
        ++loads;
        return history;
    }
    std::unique_ptr<Market::Core::Provider::Subscription> subscribe(const Market::Core::Series::Key&,
                                                                    Market::Core::Provider::UpdateHandler) override {
        return std::make_unique<NoSubscription>();
    }
};

Timestamp at(std::int64_t seconds) {
    return Timestamp{std::chrono::seconds{seconds}};
}

std::shared_ptr<ConditionExpression> market(std::string, std::string, double);
std::shared_ptr<ConditionExpression> indicator(std::string, std::string, double);
Definition definition();
Bar bar(std::int64_t, std::int64_t, double,
        Market::Core::Series::BarState state = Market::Core::Series::BarState::Closed);

int test_warmup_includes_analyzer_and_all_sequence_limits() {
    Analyzer analyzer;
    auto model = definition();
    auto sequence = [](std::string id, std::optional<Duration> elapsed, std::optional<std::uint64_t> bars) {
        auto value = std::make_shared<ConditionExpression>();
        value->id = std::move(id);
        value->kind = ConditionKind::Sequence;
        value->sequence = {elapsed, bars};
        value->children = {market(value->id + "-leaf", "primary", 0)};
        return value;
    };
    model.entry.condition = sequence("entry-sequence", {}, 4);
    model.exits = {{"exit", sequence("exit-sequence", 50min, {})}};
    model.runtime_rules = {{"rule", 1, sequence("rule-sequence", {}, 6), {}}};
    DataGraph graph(Snapshot{model}, analyzer, "AAPL");
    // Three analyzer bars plus the largest six-bar sequence window, sharing the current bar.
    CPPTEST_ASSERT(graph.required_history("primary") == 8);
    return 0;
}

int test_identical_calculation_identity_is_executed_once() {
    Analyzer analyzer;
    auto model = definition();
    model.indicators = {{"first", "primary", "identity", {}}, {"second", "primary-copy", "identity", {}}};
    model.entry.condition = indicator("uses-first", "first", 0);
    DataGraph graph(Snapshot{model}, analyzer, "AAPL");
    graph.set_series("primary", std::vector{bar(0, 600, 10), bar(600, 600, 11)});
    CPPTEST_ASSERT(graph.evaluate_entry().size() == 2);
    CPPTEST_ASSERT(analyzer.calculations == 1);
    CPPTEST_ASSERT(graph.indicator_value("first", "value", at(1200)) ==
                   graph.indicator_value("second", "value", at(1200)));
    (void)graph.evaluate(model.entry.condition, at(1200));
    (void)graph.evaluate(model.entry.condition, at(1200), RunValues{});
    CPPTEST_ASSERT(analyzer.calculations == 1);
    return 0;
}

Bar bar(std::int64_t open, std::int64_t duration, double close, Market::Core::Series::BarState state) {
    return {at(open), at(open + duration), close - 1, close + 1, close - 2, close, 1000, state};
}

std::shared_ptr<ConditionExpression> market(std::string id, std::string series, double threshold) {
    auto value = std::make_shared<ConditionExpression>();
    value->id = std::move(id);
    value->kind = ConditionKind::MarketComparison;
    value->predicate = MarketComparison{std::move(series), MarketField::Close, Comparison::Greater, threshold};
    return value;
}

std::shared_ptr<ConditionExpression> indicator(std::string id, std::string binding, double threshold) {
    auto value = std::make_shared<ConditionExpression>();
    value->id = std::move(id);
    value->kind = ConditionKind::IndicatorComparison;
    value->predicate = IndicatorComparison{std::move(binding), "value", Comparison::Greater, threshold};
    return value;
}

std::shared_ptr<ConditionExpression> pattern(std::string id, std::string binding) {
    auto value = std::make_shared<ConditionExpression>();
    value->id = std::move(id);
    value->kind = ConditionKind::PatternOccurrence;
    value->predicate = PatternOccurrence{std::move(binding), Direction::Long};
    return value;
}

std::shared_ptr<ConditionExpression> cross(std::string id, std::string left, std::string right) {
    auto value = std::make_shared<ConditionExpression>();
    value->id = std::move(id);
    value->kind = ConditionKind::IndicatorCross;
    value->predicate = IndicatorCross{std::move(left), "value", std::move(right), "value", {}, CrossDirection::Above};
    return value;
}

std::shared_ptr<ConditionExpression> constant_cross(std::string id, std::string left, double right) {
    auto value = std::make_shared<ConditionExpression>();
    value->id = std::move(id);
    value->kind = ConditionKind::IndicatorCross;
    value->predicate = IndicatorCross{std::move(left), "value", {}, {}, right, CrossDirection::Above};
    return value;
}

Definition definition() {
    Definition value;
    value.id = "phase-3";
    value.display_name = "Phase 3 fixture";
    value.primary_series_id = "primary";
    value.series = {{"primary", "fake", {InstrumentKind::Subject, {}}, {10, Market::Core::Time::Unit::Minute}, 20min},
                    {"primary-copy", "fake", {InstrumentKind::Subject, {}}, {10, Market::Core::Time::Unit::Minute}, {}},
                    {"hourly", "fake", {InstrumentKind::Subject, {}}, {1, Market::Core::Time::Unit::Hour}, {}},
                    {"peer", "fake", {InstrumentKind::Fixed, "XLK"}, {1, Market::Core::Time::Unit::Day}, 48h}};
    value.indicators = {{"slow", "primary", "identity", {{"period", std::int64_t{3}}}}};
    value.entry.condition = std::make_shared<ConditionExpression>();
    value.entry.condition->id = "all";
    value.entry.condition->kind = ConditionKind::All;
    value.entry.condition->children = {market("subject-up", "primary", 10), market("peer-up", "peer", 50)};
    value.entry.order = {EntryOrderKind::NextBarOpen};
    value.stop_loss = {PricePolicyKind::Absolute, 1};
    value.target = {PricePolicyKind::Absolute, 2};
    return value;
}

bool evaluations_equal(const std::vector<Evaluation>& left, const std::vector<Evaluation>& right) {
    if (left.size() != right.size())
        return false;

    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i].time != right[i].time || left[i].truth != right[i].truth ||
            left[i].evidence.size() != right[i].evidence.size())
            return false;

        for (std::size_t j = 0; j < left[i].evidence.size(); ++j) {
            const auto& a = left[i].evidence[j];
            const auto& b = right[i].evidence[j];
            if (a.condition_id != b.condition_id || a.truth != b.truth || a.value != b.value ||
                a.source_time != b.source_time || a.detail != b.detail)
                return false;
        }
    }

    return true;
}
} // namespace

int test_resolution_warmup_graph_reuse_and_derived_timeframe() {
    Analyzer analyzer;
    auto model = definition();
    DataGraph graph(Snapshot{model}, analyzer, "AAPL");
    CPPTEST_ASSERT(graph.resolution().errors.empty());
    CPPTEST_ASSERT(graph.resolution().series[0].key.instrument == "AAPL");
    CPPTEST_ASSERT(graph.resolution().series[3].key.instrument == "XLK");
    CPPTEST_ASSERT(graph.unique_series_count() == 3);
    CPPTEST_ASSERT(graph.required_history("primary") == 3);
    CPPTEST_ASSERT(!graph.dependency_order().empty());

    const std::vector bars{bar(0, 600, 10),    bar(600, 600, 11),  bar(1200, 600, 12),
                           bar(1800, 600, 13), bar(2400, 600, 14), bar(3000, 600, 15)};
    graph.set_series("primary", bars);
    graph.derive_series("hourly", "primary");
    CPPTEST_ASSERT(graph.resolution().series[2].derived);
    CPPTEST_ASSERT(graph.state("primary").readiness == Readiness::Ready);
    return 0;
}

int test_publication_alignment_unknown_stale_and_closed_clock() {
    Analyzer analyzer;
    DataGraph graph(Snapshot{definition()}, analyzer, "AAPL");
    const std::vector primary{bar(0, 600, 11), bar(600, 600, 12), bar(1200, 600, 13),
                              bar(1800, 600, 99, Market::Core::Series::BarState::Forming)};
    // This daily value only becomes public at t=1800, so it cannot affect t=600 or t=1200.
    const std::vector peer{bar(-84600, 86400, 60)};
    graph.set_series("primary", primary);
    graph.set_series("peer", peer);
    const auto result = graph.evaluate_entry();
    CPPTEST_ASSERT(result.size() == 3);
    CPPTEST_ASSERT(result[0].truth == Truth::Unknown && result[1].truth == Truth::Unknown);
    CPPTEST_ASSERT(result[2].truth == Truth::True);
    CPPTEST_ASSERT(result[2].evidence.size() == 2 && result[2].evidence[1].source_time == at(1800));
    return 0;
}

int test_arrival_order_dirty_replay_and_error_readiness_are_deterministic() {
    Analyzer analyzer;
    const std::vector primary{bar(0, 600, 11), bar(600, 600, 12), bar(1200, 600, 13)};
    const std::vector peer{bar(-84600, 86400, 60)};
    DataGraph first(Snapshot{definition()}, analyzer, "AAPL");
    first.set_series("peer", peer, 1);
    first.set_series("primary", primary, 1);
    const auto expected = first.evaluate_entry();

    DataGraph second(Snapshot{definition()}, analyzer, "AAPL");
    second.set_series("primary", primary, 1);
    second.set_series("peer", peer, 1);
    CPPTEST_ASSERT(second.evaluate_entry().back().truth == expected.back().truth);
    auto corrected = primary;
    corrected.back().close = 9;
    second.set_series("primary", corrected, 2);
    CPPTEST_ASSERT(second.evaluate_entry().back().truth == Truth::False);
    DataGraph replay(Snapshot{definition()}, analyzer, "AAPL");
    replay.set_series("primary", corrected, 2);
    replay.set_series("peer", peer, 1);
    CPPTEST_ASSERT(replay.evaluate_entry().back().truth == second.evaluate_entry().back().truth);
    second.set_provider_error("peer", "fixture failure");
    CPPTEST_ASSERT(second.state("peer").readiness == Readiness::ProviderError);
    return 0;
}

int test_same_count_replacements_use_open_time_and_match_full_replay() {
    const std::vector original{bar(0, 600, 10), bar(600, 600, 11), bar(1200, 600, 12)};
    using Mutation = void (*)(Bar&);
    const std::vector<Mutation> mutations{+[](Bar& value) { value.high += 7; },
                                          +[](Bar& value) { value.low -= 7; },
                                          +[](Bar& value) { value.open += 7; },
                                          +[](Bar& value) { value.volume += 7; },
                                          +[](Bar& value) { value.close += 7; },
                                          +[](Bar& value) { value.close_time = at(1900); },
                                          +[](Bar& value) { value.state = Market::Core::Series::BarState::Forming; }};
    for (const auto mutate : mutations) {
        Analyzer incremental_analyzer;
        auto model = definition();
        model.indicators = {{"value", "primary", "identity", {}}};
        model.entry.condition = indicator("value-condition", "value", 0);
        DataGraph incremental(Snapshot{model}, incremental_analyzer, "AAPL");
        incremental.set_series("primary", original, 1);
        (void)incremental.evaluate_entry();
        auto corrected = original;
        mutate(corrected[1]);
        incremental.set_series("primary", corrected, 2);
        const auto actual = incremental.evaluate_entry();
        CPPTEST_ASSERT(incremental_analyzer.dirty_ranges.back());
        CPPTEST_ASSERT(incremental_analyzer.dirty_ranges.back()->begin == corrected[1].open_time);

        Analyzer replay_analyzer;
        DataGraph replay(Snapshot{model}, replay_analyzer, "AAPL");
        replay.set_series("primary", corrected, 2);
        CPPTEST_ASSERT(evaluations_equal(actual, replay.evaluate_entry()));
        for (const auto& item : actual)
            CPPTEST_ASSERT(incremental.indicator_value("value", "value", item.time) ==
                           replay.indicator_value("value", "value", item.time));
    }
    return 0;
}

int test_open_time_moves_dirty_both_old_and_new_bucket_coordinates() {
    for (const auto replacement : {-60, 3660}) {
        Analyzer analyzer;
        auto model = definition();
        model.indicators = {{"value", "primary", "identity", {}}};
        model.entry.condition = indicator("value-condition", "value", 0);
        DataGraph graph(Snapshot{model}, analyzer, "AAPL");
        auto input = std::vector{bar(0, 600, 10), bar(600, 600, 11), bar(1200, 600, 12)};
        graph.set_series("primary", input, 1);
        (void)graph.evaluate_entry();
        input[1].open_time = at(replacement);
        input[1].close_time = at(replacement + 600);
        graph.set_series("primary", input, 2);
        (void)graph.evaluate_entry();
        const auto dirty = analyzer.dirty_ranges.back();
        CPPTEST_ASSERT(dirty);
        CPPTEST_ASSERT(dirty->begin == at(std::min<std::int64_t>(600, replacement)));
        CPPTEST_ASSERT(dirty->end > at(std::max<std::int64_t>(600, replacement)));
    }
    return 0;
}

int test_apply_update_dirty_ranges_are_not_seeded_by_history_reset() {
    Analyzer analyzer;
    auto model = definition();
    model.indicators = {{"value", "primary", "identity", {}}};
    model.entry.condition = indicator("value-condition", "value", 0);
    DataGraph graph(Snapshot{model}, analyzer, "AAPL");
    graph.set_series("primary", std::vector{bar(0, 600, 10), bar(600, 600, 11)}, 1);
    (void)graph.evaluate_entry();
    const auto key = graph.resolution().series.front().key;
    const auto matches_replay = [&] {
        Analyzer replay_analyzer;
        DataGraph replay(Snapshot{model}, replay_analyzer, "AAPL");
        const auto current = graph.primary_bars();
        replay.set_series("primary", current, 99);
        const auto actual = graph.evaluate_entry();
        const auto expected = replay.evaluate_entry();
        if (!evaluations_equal(actual, expected))
            return false;

        for (const auto& item : actual)
            if (graph.indicator_value("value", "value", item.time) !=
                replay.indicator_value("value", "value", item.time))
                return false;

        return true;
    };
    graph.apply({key, Market::Core::Series::BarUpdateKind::AppendClosed, {bar(1200, 600, 12)}});
    (void)graph.evaluate_entry();
    CPPTEST_ASSERT(analyzer.dirty_ranges.back()->begin == at(1200));
    CPPTEST_ASSERT(matches_replay());

    graph.apply({key, Market::Core::Series::BarUpdateKind::Backfill, {bar(600, 600, 21)}});
    (void)graph.evaluate_entry();
    CPPTEST_ASSERT(analyzer.dirty_ranges.back()->begin == at(600));
    CPPTEST_ASSERT(matches_replay());
    auto forming = bar(1200, 600, 15, Market::Core::Series::BarState::Forming);
    graph.apply({key, Market::Core::Series::BarUpdateKind::Backfill, {forming}});
    (void)graph.evaluate_entry();
    forming.high += 2;
    graph.apply({key, Market::Core::Series::BarUpdateKind::ReplaceForming, {forming}});
    (void)graph.evaluate_entry();
    CPPTEST_ASSERT(analyzer.dirty_ranges.back()->begin == at(1200));
    CPPTEST_ASSERT(matches_replay());
    graph.apply({key, Market::Core::Series::BarUpdateKind::Reset, {bar(3000, 600, 30)}});
    (void)graph.evaluate_entry();
    CPPTEST_ASSERT(analyzer.dirty_ranges.back()->begin == at(0));
    CPPTEST_ASSERT(matches_replay());
    return 0;
}

int test_derived_series_refreshes_and_propagates_bucket_dirty_range() {
    Analyzer analyzer;
    auto model = definition();
    model.indicators = {{"hour-value", "hourly", "identity", {}}};
    model.entry.condition = indicator("hour-condition", "hour-value", 0);
    DataGraph graph(Snapshot{model}, analyzer, "AAPL");
    graph.derive_series("hourly", "primary");
    std::vector source{bar(0, 600, 10),    bar(600, 600, 11),  bar(1200, 600, 12),
                       bar(1800, 600, 13), bar(2400, 600, 14), bar(3000, 600, 15)};
    graph.set_series("primary", source, 1);
    (void)graph.evaluate_entry();
    CPPTEST_ASSERT(graph.derived_refresh("hourly").refresh_count == 1);
    CPPTEST_ASSERT(graph.indicator_value("hour-value", "value", at(3600)) == 15);
    const auto key = graph.resolution().series.front().key;

    graph.apply({key, Market::Core::Series::BarUpdateKind::AppendClosed, {bar(3600, 600, 20)}});
    (void)graph.evaluate_entry();
    CPPTEST_ASSERT(graph.derived_refresh("hourly").refresh_count == 2);
    CPPTEST_ASSERT(graph.derived_refresh("hourly").source_begin == 6);
    CPPTEST_ASSERT(graph.derived_refresh("hourly").source_count == 1);
    CPPTEST_ASSERT(graph.derived_refresh("hourly").reused_prefix == 1);
    CPPTEST_ASSERT(analyzer.dirty_ranges.back()->begin == at(3600));
    graph.apply({key, Market::Core::Series::BarUpdateKind::Backfill, {bar(1200, 600, 30)}});
    (void)graph.evaluate_entry();
    CPPTEST_ASSERT(analyzer.dirty_ranges.back()->begin == at(0));
    CPPTEST_ASSERT(graph.indicator_value("hour-value", "value", at(3600)) == 15);

    auto forming = bar(3600, 600, 25, Market::Core::Series::BarState::Forming);
    graph.apply({key, Market::Core::Series::BarUpdateKind::Backfill, {forming}});
    (void)graph.evaluate_entry();
    forming.close = 26;
    graph.apply({key, Market::Core::Series::BarUpdateKind::ReplaceForming, {forming}});
    (void)graph.evaluate_entry();
    CPPTEST_ASSERT(analyzer.dirty_ranges.back()->begin == at(3600));
    graph.apply({key, Market::Core::Series::BarUpdateKind::Reset, source});
    (void)graph.evaluate_entry();
    CPPTEST_ASSERT(analyzer.dirty_ranges.back()->begin == at(0));
    return 0;
}

int test_provider_history_and_updates_use_provider_neutral_contracts() {
    Analyzer analyzer;
    DataGraph graph(Snapshot{definition()}, analyzer, "AAPL");
    FakeProvider provider;
    provider.history = {bar(0, 600, 11), bar(600, 600, 12), bar(1200, 600, 13)};
    graph.load(provider, "primary", {at(0), at(2400)});
    CPPTEST_ASSERT(provider.loads == 1 && graph.state("primary").readiness == Readiness::Ready);

    const auto key = graph.resolution().series.front().key;
    graph.apply({key, Market::Core::Series::BarUpdateKind::AppendClosed, {bar(1800, 600, 14)}});
    FakeProvider peer_provider;
    peer_provider.history = {bar(-84000, 86400, 60)};
    graph.load(peer_provider, "peer", {at(-90000), at(3000)});
    graph.derive_series("hourly", "primary");
    CPPTEST_ASSERT(peer_provider.loads == 1 && graph.resolution().series[2].derived);
    CPPTEST_ASSERT(graph.evaluate_entry().size() == 4);

    graph.apply({key, Market::Core::Series::BarUpdateKind::AppendClosed, {bar(2400, 600, 15), bar(3000, 600, 16)}});
    CPPTEST_ASSERT(graph.state("hourly").available_history == 1);

    DataGraph stale(Snapshot{definition()}, analyzer, "AAPL");
    const std::vector late_primary{bar(3 * 86400, 600, 20), bar(3 * 86400 + 600, 600, 21),
                                   bar(3 * 86400 + 1200, 600, 22)};
    stale.set_series("primary", late_primary);
    stale.set_series("peer", peer_provider.history);
    CPPTEST_ASSERT(stale.state("peer").readiness == Readiness::Stale);
    return 0;
}

int test_forming_indicator_is_unknown_until_the_source_bar_closes() {
    Analyzer analyzer;
    auto model = definition();
    model.indicators.push_back({"peer-value", "peer", "identity", {}});
    model.entry.condition = indicator("peer-indicator", "peer-value", 50);
    DataGraph graph(Snapshot{model}, analyzer, "AAPL");
    graph.set_series("primary", std::vector{bar(0, 600, 20), bar(600, 600, 21)});
    graph.set_series("peer", std::vector{bar(-85200, 86400, 60, Market::Core::Series::BarState::Forming)});

    const auto forming = graph.evaluate_entry();
    CPPTEST_ASSERT(forming.back().truth == Truth::Unknown);
    CPPTEST_ASSERT(forming.back().evidence.front().detail == "closed indicator input is unavailable");
    graph.set_series("peer", std::vector{bar(-85200, 86400, 60)}, 2);
    const auto closed = graph.evaluate_entry();
    CPPTEST_ASSERT(closed.back().truth == Truth::True);
    CPPTEST_ASSERT(closed.back().evidence.front().source_time == at(1200));
    return 0;
}

int test_forming_pattern_cannot_advance_sequence_and_closed_occurrence_is_consumed_once() {
    Analyzer analyzer;
    auto model = definition();
    model.series[3].maximum_data_age.reset();
    model.indicators.push_back({"peer-pattern", "peer", "pattern", {}});
    auto sequence = std::make_shared<ConditionExpression>();
    sequence->id = "pattern-then-price";
    sequence->kind = ConditionKind::Sequence;
    sequence->children = {pattern("peer-occurrence", "peer-pattern"), market("confirmation", "primary", 10)};
    model.entry.condition = sequence;
    DataGraph graph(Snapshot{model}, analyzer, "AAPL");
    graph.set_series("primary", std::vector{bar(0, 600, 20), bar(600, 600, 21), bar(1200, 600, 22)});
    graph.set_series("peer", std::vector{bar(-85200, 86400, 60, Market::Core::Series::BarState::Forming)});

    const auto forming = graph.evaluate_entry();
    CPPTEST_ASSERT(std::ranges::count(forming, Truth::True, &Evaluation::truth) == 0);
    CPPTEST_ASSERT(forming[1].truth == Truth::Unknown);
    CPPTEST_ASSERT(std::ranges::any_of(forming[1].evidence, [](const auto& item) {
        return item.condition_id == "peer-occurrence" && item.detail == "closed pattern input is unavailable";
    }));
    graph.set_series("peer", std::vector{bar(-85200, 86400, 60)}, 2);
    const auto closed = graph.evaluate_entry();
    CPPTEST_ASSERT(std::ranges::count(closed, Truth::True, &Evaluation::truth) == 1);
    CPPTEST_ASSERT(closed.back().truth == Truth::True);
    CPPTEST_ASSERT(std::ranges::count(graph.evaluate_entry(), Truth::True, &Evaluation::truth) == 1);
    return 0;
}

int test_secondary_freshness_applies_to_market_indicator_cross_and_pattern_leaves() {
    Analyzer analyzer;
    auto model = definition();
    model.series[3].maximum_data_age = 5min;
    model.indicators = {{"primary-value", "primary", "identity", {}},
                        {"peer-value", "peer", "identity", {}},
                        {"peer-pattern", "peer", "pattern", {}}};
    DataGraph graph(Snapshot{model}, analyzer, "AAPL");
    graph.set_series("primary", std::vector{bar(0, 600, 40), bar(600, 600, 70), bar(1200, 600, 80)});
    graph.set_series("peer", std::vector{bar(-85800, 86400, 50), bar(600, 600, 60)});

    const auto assert_fresh_then_stale = [&](const std::shared_ptr<ConditionExpression>& condition, Truth expected,
                                             std::string_view stale_detail) -> int {
        const auto fresh = graph.evaluate(condition, at(1200));
        CPPTEST_ASSERT(fresh.truth == expected);
        CPPTEST_ASSERT(!fresh.evidence.empty() && fresh.evidence.front().source_time == at(1200));
        const auto stale = graph.evaluate(condition, at(1800));
        CPPTEST_ASSERT(stale.truth == Truth::Unknown);
        CPPTEST_ASSERT(!stale.evidence.empty() && stale.evidence.front().detail == stale_detail);
        return 0;
    };
    CPPTEST_ASSERT(assert_fresh_then_stale(market("peer-market", "peer", 40), Truth::True, "input is stale") == 0);
    CPPTEST_ASSERT(assert_fresh_then_stale(indicator("peer-indicator", "peer-value", 40), Truth::True,
                                           "indicator input is stale") == 0);
    CPPTEST_ASSERT(assert_fresh_then_stale(cross("stale-left", "peer-value", "primary-value"), Truth::False,
                                           "current left cross input is stale") == 0);
    CPPTEST_ASSERT(assert_fresh_then_stale(cross("stale-right", "primary-value", "peer-value"), Truth::True,
                                           "current right cross input is stale") == 0);
    CPPTEST_ASSERT(assert_fresh_then_stale(pattern("peer-pattern-occurrence", "peer-pattern"), Truth::True,
                                           "pattern input is stale") == 0);
    return 0;
}

int test_cross_previous_inputs_require_fresh_available_samples() {
    Analyzer analyzer;
    auto model = definition();
    model.series[0].maximum_data_age = 5min;
    model.series[3].maximum_data_age = 5min;
    model.indicators = {{"primary-value", "primary", "identity", {}}, {"peer-value", "peer", "identity", {}}};
    DataGraph graph(Snapshot{model}, analyzer, "AAPL");
    graph.set_series("primary", std::vector{bar(0, 600, 40), bar(600, 600, 70)});
    graph.set_series("peer", std::vector{bar(-600, 600, 50), bar(600, 600, 60)});

    const auto stale_previous_left = graph.evaluate(cross("cross", "peer-value", "primary-value"), at(1200));
    CPPTEST_ASSERT(stale_previous_left.truth == Truth::Unknown);
    CPPTEST_ASSERT(stale_previous_left.evidence.front().detail == "previous left cross input is stale");

    const auto stale_previous_right = graph.evaluate(cross("cross", "primary-value", "peer-value"), at(1200));
    CPPTEST_ASSERT(stale_previous_right.truth == Truth::Unknown);
    CPPTEST_ASSERT(stale_previous_right.evidence.front().detail == "previous right cross input is stale");

    const auto constant_right = graph.evaluate(constant_cross("constant-cross", "primary-value", 50), at(1200));
    CPPTEST_ASSERT(constant_right.truth == Truth::True);
    CPPTEST_ASSERT(constant_right.evidence.front().source_time == at(1200));
    return 0;
}

int test_cross_missing_previous_inputs_have_actionable_evidence() {
    Analyzer analyzer;
    auto model = definition();
    model.series[0].maximum_data_age.reset();
    model.series[3].maximum_data_age.reset();
    model.indicators = {{"primary-value", "primary", "identity", {}}, {"peer-value", "peer", "identity", {}}};

    DataGraph missing_left(Snapshot{model}, analyzer, "AAPL");
    missing_left.set_series("primary", std::vector{bar(0, 600, 40), bar(600, 600, 70)});
    missing_left.set_series("peer", std::vector{bar(600, 600, 60)});
    const auto left = missing_left.evaluate(cross("cross", "peer-value", "primary-value"), at(1200));
    CPPTEST_ASSERT(left.truth == Truth::Unknown);
    CPPTEST_ASSERT(left.evidence.front().detail == "closed previous left cross input is unavailable");

    DataGraph missing_right(Snapshot{model}, analyzer, "AAPL");
    missing_right.set_series("primary", std::vector{bar(0, 600, 40), bar(600, 600, 70)});
    missing_right.set_series("peer", std::vector{bar(600, 600, 60)});
    const auto right = missing_right.evaluate(cross("cross", "primary-value", "peer-value"), at(1200));
    CPPTEST_ASSERT(right.truth == Truth::Unknown);
    CPPTEST_ASSERT(right.evidence.front().detail == "closed previous right cross input is unavailable");
    return 0;
}

int test_fresh_cross_truth_and_source_time_are_exact() {
    Analyzer analyzer;
    auto model = definition();
    model.series[3].maximum_data_age = 10min;
    model.indicators = {{"primary-value", "primary", "identity", {}}, {"peer-value", "peer", "identity", {}}};
    DataGraph graph(Snapshot{model}, analyzer, "AAPL");
    graph.set_series("primary", std::vector{bar(0, 600, 40), bar(600, 600, 70), bar(1200, 600, 80)});
    graph.set_series("peer", std::vector{bar(0, 600, 50), bar(600, 600, 60), bar(1200, 600, 65)});

    const auto above = graph.evaluate(cross("above", "primary-value", "peer-value"), at(1200));
    CPPTEST_ASSERT(above.truth == Truth::True);
    CPPTEST_ASSERT(above.evidence.front().source_time == at(1200));
    const auto not_crossing = graph.evaluate(cross("not-crossing", "primary-value", "peer-value"), at(1800));
    CPPTEST_ASSERT(not_crossing.truth == Truth::False);
    CPPTEST_ASSERT(not_crossing.evidence.front().source_time == at(1800));
    return 0;
}

int main() {
    CPPTEST_RUN(test_resolution_warmup_graph_reuse_and_derived_timeframe);
    CPPTEST_RUN(test_publication_alignment_unknown_stale_and_closed_clock);
    CPPTEST_RUN(test_arrival_order_dirty_replay_and_error_readiness_are_deterministic);
    CPPTEST_RUN(test_same_count_replacements_use_open_time_and_match_full_replay);
    CPPTEST_RUN(test_open_time_moves_dirty_both_old_and_new_bucket_coordinates);
    CPPTEST_RUN(test_apply_update_dirty_ranges_are_not_seeded_by_history_reset);
    CPPTEST_RUN(test_derived_series_refreshes_and_propagates_bucket_dirty_range);
    CPPTEST_RUN(test_provider_history_and_updates_use_provider_neutral_contracts);
    CPPTEST_RUN(test_forming_indicator_is_unknown_until_the_source_bar_closes);
    CPPTEST_RUN(test_forming_pattern_cannot_advance_sequence_and_closed_occurrence_is_consumed_once);
    CPPTEST_RUN(test_cross_previous_inputs_require_fresh_available_samples);
    CPPTEST_RUN(test_cross_missing_previous_inputs_have_actionable_evidence);
    CPPTEST_RUN(test_fresh_cross_truth_and_source_time_are_exact);
    CPPTEST_RUN(test_warmup_includes_analyzer_and_all_sequence_limits);
    CPPTEST_RUN(test_identical_calculation_identity_is_executed_once);
    CPPTEST_RUN(test_secondary_freshness_applies_to_market_indicator_cross_and_pattern_leaves);
    return 0;
}
