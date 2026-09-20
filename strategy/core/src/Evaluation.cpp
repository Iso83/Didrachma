#include <Didrachma/analysis/core/multiTimeframe/Analysis.h>
#include <Didrachma/market/core/series/Bars.h>
#include <Didrachma/market/core/series/Resampler.h>
#include <Didrachma/strategy/core/Evaluation.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace Didrachma::Strategy::Core::Intern {
using Timestamp = Market::Core::Time::UtcTimestamp;
using Bar = Market::Core::Series::Bar;
using TimedValue = Analysis::Core::MultiTimeframe::TimedValue;

std::string key_text(const Market::Core::Series::Key& key) {
    return key.provider + "\n" + key.instrument + "\n" + std::to_string(key.timeframe.quantity) + ":" +
           std::to_string(static_cast<int>(key.timeframe.unit));
}

std::chrono::seconds frame_duration(Market::Core::Time::Frame frame) {
    using enum Market::Core::Time::Unit;
    if (frame.unit == Minute)
        return std::chrono::minutes{frame.quantity};

    if (frame.unit == Hour)
        return std::chrono::hours{frame.quantity};

    return std::chrono::days{frame.quantity};
}

bool compare(double left, Comparison operation, double right) {
    switch (operation) {
        case Comparison::Less:
            return left < right;
        case Comparison::LessOrEqual:
            return left <= right;
        case Comparison::Equal:
            return left == right;
        case Comparison::GreaterOrEqual:
            return left >= right;
        case Comparison::Greater:
            return left > right;
    }

    return false;
}

Truth combine_all(std::span<const Truth> values) {
    if (std::ranges::find(values, Truth::False) != values.end())
        return Truth::False;

    return std::ranges::find(values, Truth::Unknown) != values.end() ? Truth::Unknown : Truth::True;
}

Truth combine_any(std::span<const Truth> values) {
    if (std::ranges::find(values, Truth::True) != values.end())
        return Truth::True;

    return std::ranges::find(values, Truth::Unknown) != values.end() ? Truth::Unknown : Truth::False;
}

const Bar* aligned_bar(std::span<const Bar> bars, Timestamp time) {
    const Bar* result{};
    for (const auto& bar : bars) {
        if (bar.state != Market::Core::Series::BarState::Closed || !bar.close_time || *bar.close_time > time)
            continue;

        if (!result || *bar.close_time > *result->close_time)
            result = &bar;
    }
    return result;
}

double field(const Bar& bar, MarketField value) {
    switch (value) {
        case MarketField::Open:
            return bar.open;
        case MarketField::High:
            return bar.high;
        case MarketField::Low:
            return bar.low;
        case MarketField::Close:
            return bar.close;
        case MarketField::Volume:
            return bar.volume;
    }
    return {};
}
} // namespace Didrachma::Strategy::Core::Intern

namespace Didrachma::Strategy::Core {
struct DataGraph::Implementation {
    struct StoredSeries {
        Market::Core::Series::Key key;
        std::vector<Intern::Bar> bars;
        std::uint64_t revision{};
        std::string error;
    };
    struct IndicatorData {
        Analysis::Core::Indicator::Result result;
        std::map<std::string, std::vector<Intern::TimedValue>> values;
    };

    Snapshot snapshot;
    Analysis::Core::Indicator::Analyzer* analyzer;
    Resolution resolved;
    std::map<std::string, std::shared_ptr<StoredSeries>> by_binding;
    std::map<std::string, std::shared_ptr<StoredSeries>> shared;
    std::map<std::string, std::string> derived_from;
    std::map<std::string, IndicatorData> indicators;
    Analysis::Core::MultiTimeframe::DependencyGraph graph;
    std::vector<Evaluation> cached;
    std::optional<Intern::Timestamp> dirty_from;

    Implementation(Snapshot value, Analysis::Core::Indicator::Analyzer& analysis, std::optional<std::string> subject)
        : snapshot(std::move(value)), analyzer(&analysis),
          resolved(resolve(snapshot.definition(), subject ? std::optional<std::string_view>{*subject} : std::nullopt)) {
        for (const auto& item : resolved.series) {
            const auto encoded = Intern::key_text(item.key);
            auto& storage = shared[encoded];
            if (!storage)
                storage = std::make_shared<StoredSeries>(StoredSeries{item.key});
            by_binding[item.binding_id] = storage;
            graph.add({"series:" + item.binding_id, Analysis::Core::MultiTimeframe::NodeKind::Series, {}});
        }
        for (const auto& item : snapshot.definition().indicators)
            graph.add({"indicator:" + item.id,
                       Analysis::Core::MultiTimeframe::NodeKind::Indicator,
                       {"series:" + item.series_id},
                       Intern::frame_duration(series_binding(item.series_id)->timeframe) *
                           static_cast<long>(indicator_history(item))});
        add_conditions(snapshot.definition().entry.condition);
    }

    const SeriesBinding* series_binding(std::string_view id) const {
        const auto& items = snapshot.definition().series;
        const auto found = std::ranges::find(items, id, &SeriesBinding::id);
        return found == items.end() ? nullptr : &*found;
    }

    std::size_t indicator_history(const IndicatorBinding& binding) const {
        std::size_t result{1};
        for (const auto& [name, value] : binding.parameters) {
            (void)name;
            if (const auto* integer = std::get_if<std::int64_t>(&value); integer && *integer > 0)
                result = std::max(result, static_cast<std::size_t>(*integer));
        }

        return result;
    }

    void add_conditions(const std::shared_ptr<ConditionExpression>& condition) {
        if (!condition)
            return;

        std::vector<std::string> dependencies;
        if (const auto* value = std::get_if<MarketComparison>(&condition->predicate))
            dependencies.push_back("series:" + value->series_id);
        if (const auto* value = std::get_if<IndicatorComparison>(&condition->predicate))
            dependencies.push_back("indicator:" + value->indicator_id);
        if (const auto* value = std::get_if<IndicatorCross>(&condition->predicate)) {
            dependencies.push_back("indicator:" + value->left_indicator_id);
            if (value->right_indicator_id)
                dependencies.push_back("indicator:" + *value->right_indicator_id);
        }
        if (const auto* value = std::get_if<PatternOccurrence>(&condition->predicate))
            dependencies.push_back("indicator:" + value->indicator_id);
        for (const auto& child : condition->children) {
            add_conditions(child);
            dependencies.push_back("condition:" + child->id);
        }
        graph.add({"condition:" + condition->id, Analysis::Core::MultiTimeframe::NodeKind::Condition,
                   std::move(dependencies)});
    }

    std::size_t required(std::string_view binding_id) const {
        std::size_t result{1};
        for (const auto& indicator : snapshot.definition().indicators)
            if (indicator.series_id == binding_id)
                result = std::max(result, indicator_history(indicator));
        const auto sequence_history = [&](const auto& self,
                                          const std::shared_ptr<ConditionExpression>& condition) -> void {
            if (!condition)
                return;
            if (condition->kind == ConditionKind::Sequence && condition->sequence.maximum_closed_bars)
                result = std::max(result, static_cast<std::size_t>(*condition->sequence.maximum_closed_bars));
            for (const auto& child : condition->children)
                self(self, child);
        };
        sequence_history(sequence_history, snapshot.definition().entry.condition);
        return result;
    }

    void calculate() {
        indicators.clear();
        for (const auto& binding : snapshot.definition().indicators) {
            const auto found = by_binding.find(binding.series_id);
            if (found == by_binding.end() || found->second->bars.empty())
                continue;

            Analysis::Core::Indicator::Instance instance{binding.id, binding.definition_id, true, binding.parameters};
            auto outcome = analyzer->calculate({instance, found->second->bars, found->second->revision, {}});
            auto& destination = indicators[binding.id];
            destination.result = std::move(outcome.result);
            for (const auto& output : destination.result.outputs) {
                auto& values = destination.values[output.output_id];
                for (const auto& sample : output.samples) {
                    const auto bar = std::ranges::find(found->second->bars, sample.timestamp, &Intern::Bar::open_time);
                    const auto available =
                        bar != found->second->bars.end() && bar->close_time ? *bar->close_time : sample.timestamp;
                    values.push_back({sample.timestamp, available, sample.value, true});
                }
            }
        }
    }

    const Intern::TimedValue* indicator_value(std::string_view id, std::string_view output,
                                              Intern::Timestamp time) const {
        const auto found = indicators.find(std::string{id});
        if (found == indicators.end() ||
            found->second.result.state != Analysis::Core::Indicator::CalculationState::Ready)
            return nullptr;

        const auto series = std::ranges::find(found->second.result.outputs, output,
                                              &Analysis::Core::Indicator::OutputSeries::output_id);
        if (series == found->second.result.outputs.end())
            return nullptr;

        const auto values = found->second.values.find(std::string{output});
        return values == found->second.values.end()
                   ? nullptr
                   : Analysis::Core::MultiTimeframe::align_closed(values->second, time);
    }

    Truth leaf(const ConditionExpression& condition, Intern::Timestamp time, Evidence& evidence,
               std::optional<RunValues> run) const {
        evidence.condition_id = condition.id;
        if (const auto* value = std::get_if<MarketComparison>(&condition.predicate)) {
            const auto source = by_binding.find(value->series_id);
            const auto binding = series_binding(value->series_id);
            const auto* bar = source == by_binding.end() ? nullptr : Intern::aligned_bar(source->second->bars, time);
            if (!bar || !binding ||
                (binding->maximum_data_age && time - *bar->close_time > *binding->maximum_data_age)) {
                evidence.detail = bar ? "input is stale" : "closed input is unavailable";
                return Truth::Unknown;
            }

            evidence.value = Intern::field(*bar, value->field);
            evidence.source_time = *bar->close_time;
            return Intern::compare(*evidence.value, value->comparison, value->value) ? Truth::True : Truth::False;
        }
        const auto read_indicator = [&](std::string_view id, std::string_view output) {
            return indicator_value(id, output, time);
        };
        if (const auto* value = std::get_if<IndicatorComparison>(&condition.predicate)) {
            const auto* sample = read_indicator(value->indicator_id, value->output_id);
            if (!sample)
                return Truth::Unknown;

            evidence.value = sample->value;
            evidence.source_time = sample->available_at;
            return Intern::compare(sample->value, value->comparison, value->value) ? Truth::True : Truth::False;
        }

        if (const auto* value = std::get_if<PatternOccurrence>(&condition.predicate)) {
            const auto* sample = read_indicator(value->indicator_id, "value");
            if (!sample)
                return Truth::Unknown;

            evidence.value = sample->value;
            evidence.source_time = sample->available_at;
            const bool direction =
                !value->direction || (*value->direction == Direction::Long ? sample->value > 0 : sample->value < 0);
            return sample->value != 0 && direction ? Truth::True : Truth::False;
        }

        if (const auto* value = std::get_if<IndicatorCross>(&condition.predicate)) {
            const auto* left = read_indicator(value->left_indicator_id, value->left_output_id);
            const auto* right = value->right_indicator_id
                                    ? read_indicator(*value->right_indicator_id, value->right_output_id)
                                    : nullptr;
            if (!left || (value->right_indicator_id && !right))
                return Truth::Unknown;

            const double rhs = right ? right->value : *value->constant;
            evidence.value = left->value;
            evidence.source_time = left->available_at;
            // Crossing is edge-triggered; compare with the immediately preceding strategy-clock sample.
            const auto primary = by_binding.find(snapshot.definition().primary_series_id);
            Intern::Timestamp previous{};
            bool have_previous{};
            if (primary != by_binding.end())
                for (const auto& bar : primary->second->bars)
                    if (bar.state == Market::Core::Series::BarState::Closed && bar.close_time &&
                        *bar.close_time < time && (!have_previous || *bar.close_time > previous)) {
                        previous = *bar.close_time;
                        have_previous = true;
                    }
            if (!have_previous)
                return Truth::Unknown;

            const auto* old_left = indicator_value(value->left_indicator_id, value->left_output_id, previous);
            const auto* old_right = value->right_indicator_id
                                        ? indicator_value(*value->right_indicator_id, value->right_output_id, previous)
                                        : nullptr;
            if (!old_left || (value->right_indicator_id && !old_right))
                return Truth::Unknown;

            const double old_rhs = old_right ? old_right->value : *value->constant;
            const bool above = old_left->value <= old_rhs && left->value > rhs;
            const bool below = old_left->value >= old_rhs && left->value < rhs;
            return (value->direction == CrossDirection::Above   ? above
                    : value->direction == CrossDirection::Below ? below
                                                                : above || below)
                       ? Truth::True
                       : Truth::False;
        }

        if (!run) {
            evidence.detail = "condition requires strategy run state";
            return Truth::Unknown;
        }
        if (const auto* value = std::get_if<ElapsedTimeCondition>(&condition.predicate)) {
            evidence.value = static_cast<double>(run->elapsed.count());
            return Intern::compare(*evidence.value, value->comparison, static_cast<double>(value->duration.count()))
                       ? Truth::True
                       : Truth::False;
        }
        if (const auto* value = std::get_if<ClosedBarCountCondition>(&condition.predicate)) {
            evidence.value = static_cast<double>(run->closed_bars);
            return Intern::compare(*evidence.value, value->comparison, static_cast<double>(value->count))
                       ? Truth::True
                       : Truth::False;
        }
        if (const auto* value = std::get_if<UnrealizedReturnCondition>(&condition.predicate)) {
            const auto measured = value->kind == ReturnKind::Gain ? run->unrealized_return_percentage
                                                                  : -run->unrealized_return_percentage;
            evidence.value = measured;
            return Intern::compare(measured, value->comparison, value->percentage) ? Truth::True : Truth::False;
        }

        evidence.detail = "unsupported condition";
        return Truth::Unknown;
    }

    Truth condition(const std::shared_ptr<ConditionExpression>& expression, Intern::Timestamp time,
                    std::vector<Evidence>& evidence, std::optional<RunValues> run = {}) const {
        if (!expression)
            return Truth::Unknown;

        const bool group = expression->kind == ConditionKind::All || expression->kind == ConditionKind::Any ||
                           expression->kind == ConditionKind::Not || expression->kind == ConditionKind::Sequence;
        if (!group) {
            Evidence item;
            item.truth = leaf(*expression, time, item, run);
            evidence.push_back(item);
            return item.truth;
        }

        std::vector<Truth> children;
        for (const auto& child : expression->children)
            children.push_back(condition(child, time, evidence, run));
        if (expression->kind == ConditionKind::Not)
            return children.front() == Truth::Unknown ? Truth::Unknown
                   : children.front() == Truth::True  ? Truth::False
                                                      : Truth::True;

        if (expression->kind == ConditionKind::Any)
            return Intern::combine_any(children);

        if (expression->kind == ConditionKind::All)
            return Intern::combine_all(children);

        // Sequence state is reconstructed deterministically from prior primary closes. A step consumes one occurrence.
        std::size_t step{};
        auto started = time;
        std::uint64_t bars{};
        const auto primary = by_binding.find(snapshot.definition().primary_series_id);
        if (primary == by_binding.end())
            return Truth::Unknown;

        for (const auto& bar : primary->second->bars) {
            if (bar.state != Market::Core::Series::BarState::Closed || !bar.close_time || *bar.close_time > time)
                continue;

            if (step &&
                ((expression->sequence.maximum_elapsed &&
                  *bar.close_time - started > *expression->sequence.maximum_elapsed) ||
                 (expression->sequence.maximum_closed_bars && bars >= *expression->sequence.maximum_closed_bars)))
                step = 0;
            std::vector<Evidence> ignored;
            if (condition(expression->children[step], *bar.close_time, ignored, run) == Truth::True) {
                if (step == 0) {
                    started = *bar.close_time;
                    bars = 0;
                }
                if (++step == expression->children.size())
                    return *bar.close_time == time ? Truth::True : Truth::False;
            }

            if (step)
                ++bars;
        }

        return std::ranges::find(children, Truth::Unknown) != children.end() ? Truth::Unknown : Truth::False;
    }
};

DataGraph::DataGraph(Snapshot snapshot, Analysis::Core::Indicator::Analyzer& analyzer,
                     std::optional<std::string> subject)
    : m_implementation(std::make_shared<Implementation>(std::move(snapshot), analyzer, std::move(subject))) {}

const Resolution& DataGraph::resolution() const {
    return m_implementation->resolved;
}

std::size_t DataGraph::required_history(std::string_view id) const {
    return m_implementation->required(id);
}

std::size_t DataGraph::unique_series_count() const {
    return m_implementation->shared.size();
}

std::vector<std::string> DataGraph::dependency_order() const {
    return m_implementation->graph.order();
}

BindingState DataGraph::state(std::string_view id) const {
    const auto found = m_implementation->by_binding.find(std::string{id});
    if (found == m_implementation->by_binding.end())
        return {Readiness::ProviderError, "binding is unresolved"};

    if (!found->second->error.empty())
        return {Readiness::ProviderError, found->second->error};

    const auto required = required_history(id);
    if (found->second->bars.empty())
        return {Readiness::Loading, {}, 0, required};

    if (found->second->bars.size() < required)
        return {Readiness::InsufficientHistory, "warm-up history is incomplete", found->second->bars.size(), required};

    const auto* binding = m_implementation->series_binding(id);
    const auto primary = m_implementation->by_binding.find(m_implementation->snapshot.definition().primary_series_id);
    if (binding && binding->maximum_data_age && primary != m_implementation->by_binding.end() &&
        !primary->second->bars.empty()) {
        const auto* latest_primary = Intern::aligned_bar(primary->second->bars, Intern::Timestamp::max());
        const auto* latest = Intern::aligned_bar(found->second->bars, Intern::Timestamp::max());
        if (latest_primary && latest && *latest_primary->close_time - *latest->close_time > *binding->maximum_data_age)
            return {Readiness::Stale, "latest closed value exceeds maximum data age", found->second->bars.size(),
                    required};
    }

    for (const auto& indicator : m_implementation->snapshot.definition().indicators)
        if (indicator.series_id == id) {
            const auto calculated = m_implementation->indicators.find(indicator.id);
            if (calculated != m_implementation->indicators.end() &&
                calculated->second.result.state == Analysis::Core::Indicator::CalculationState::Error)
                return {Readiness::CalculationError,
                        calculated->second.result.error ? calculated->second.result.error->message
                                                        : "calculation failed",
                        found->second->bars.size(), required};
        }

    return {Readiness::Ready, {}, found->second->bars.size(), required};
}

void DataGraph::set_series(std::string_view id, std::span<const Market::Core::Series::Bar> bars,
                           std::uint64_t revision) {
    const auto found = m_implementation->by_binding.find(std::string{id});
    if (found == m_implementation->by_binding.end())
        return;

    std::optional<Intern::Timestamp> changed;
    const auto common = std::min(found->second->bars.size(), bars.size());
    for (std::size_t index = 0; index < common; ++index)
        if (found->second->bars[index].open_time != bars[index].open_time ||
            found->second->bars[index].close != bars[index].close ||
            found->second->bars[index].state != bars[index].state) {
            changed = found->second->bars[index].close_time.value_or(found->second->bars[index].open_time);
            break;
        }

    if (!changed && common < bars.size())
        changed = bars[common].close_time.value_or(bars[common].open_time);
    if (!changed && bars.size() < found->second->bars.size())
        changed = found->second->bars[bars.size()].close_time.value_or(found->second->bars[bars.size()].open_time);
    if (changed)
        m_implementation->dirty_from =
            m_implementation->dirty_from ? std::min(*m_implementation->dirty_from, *changed) : changed;
    found->second->bars.assign(bars.begin(), bars.end());
    std::ranges::stable_sort(found->second->bars, {}, &Intern::Bar::open_time);
    found->second->revision = revision;
    found->second->error.clear();
}

void DataGraph::set_provider_error(std::string_view id, std::string message) {
    if (const auto found = m_implementation->by_binding.find(std::string{id});
        found != m_implementation->by_binding.end())
        found->second->error = std::move(message);
}

void DataGraph::load(Market::Core::Provider::Data& provider, std::string_view id, Market::Core::Time::Range range) {
    const auto found = m_implementation->by_binding.find(std::string{id});
    if (found == m_implementation->by_binding.end())
        return;

    const auto loaded = provider.load_history({found->second->key, range});
    if (const auto* bars = std::get_if<std::vector<Market::Core::Series::Bar>>(&loaded))
        set_series(id, *bars, found->second->revision + 1);
    else
        set_provider_error(id, std::get<Market::Core::Provider::Error>(loaded).message);
}

void DataGraph::apply(const Market::Core::Series::BarUpdate& update) {
    const auto shared = m_implementation->shared.find(Intern::key_text(update.key));
    if (shared == m_implementation->shared.end())
        return;

    Market::Core::Series::Bars model(update.key);
    if (!shared->second->bars.empty())
        model.apply({update.key, Market::Core::Series::BarUpdateKind::Reset, shared->second->bars});
    if (!model.apply(update))
        return;

    shared->second->bars.assign(model.bars().begin(), model.bars().end());
    ++shared->second->revision;
    if (model.dirty_range())
        m_implementation->dirty_from = m_implementation->dirty_from
                                           ? std::min(*m_implementation->dirty_from, model.dirty_range()->begin)
                                           : std::optional{model.dirty_range()->begin};
}

void DataGraph::derive_series(std::string_view id, std::string_view source_id) {
    const auto target = m_implementation->by_binding.find(std::string{id});
    const auto source = m_implementation->by_binding.find(std::string{source_id});
    if (target == m_implementation->by_binding.end() || source == m_implementation->by_binding.end())
        return;

    const auto* target_binding = m_implementation->series_binding(id);
    const auto* source_binding = m_implementation->series_binding(source_id);
    if (!target_binding || !source_binding)
        return;

    auto result =
        Market::Core::Series::resample(source->second->bars, source_binding->timeframe, target_binding->timeframe);
    if (!result.error.empty()) {
        target->second->error = result.error;
        return;
    }

    target->second->bars = std::move(result.bars);
    target->second->revision = source->second->revision;
    if (!target->second->bars.empty())
        m_implementation->dirty_from =
            target->second->bars.front().close_time.value_or(target->second->bars.front().open_time);

    m_implementation->derived_from[std::string{id}] = source_id;
    for (auto& resolved : m_implementation->resolved.series)
        if (resolved.binding_id == id) {
            resolved.derived = true;
            resolved.source_binding_id = source_id;
        }
}

std::vector<Evaluation> DataGraph::evaluate_entry() {
    if (!m_implementation->dirty_from && !m_implementation->cached.empty())
        return m_implementation->cached;

    m_implementation->calculate();
    std::vector<Evaluation> result;
    const auto primary = m_implementation->by_binding.find(m_implementation->snapshot.definition().primary_series_id);
    if (primary == m_implementation->by_binding.end())
        return result;

    const auto dirty =
        m_implementation->cached.empty() ? std::optional<Intern::Timestamp>{} : m_implementation->dirty_from;
    if (dirty)
        std::ranges::copy_if(m_implementation->cached, std::back_inserter(result),
                             [&](const auto& item) { return item.time < *dirty; });
    for (const auto& bar : primary->second->bars) {
        if (bar.state != Market::Core::Series::BarState::Closed || !bar.close_time)
            continue;

        if (dirty && *bar.close_time < *dirty)
            continue;

        Evaluation item{*bar.close_time};
        item.truth = m_implementation->condition(m_implementation->snapshot.definition().entry.condition, item.time,
                                                 item.evidence);
        result.push_back(std::move(item));
    }
    m_implementation->cached = result;
    m_implementation->dirty_from.reset();
    return result;
}

Evaluation DataGraph::evaluate(const std::shared_ptr<ConditionExpression>& expression, Intern::Timestamp time,
                               std::optional<RunValues> run) {
    m_implementation->calculate();
    Evaluation result{time};
    result.truth = m_implementation->condition(expression, time, result.evidence, run);
    return result;
}

std::span<const Market::Core::Series::Bar> DataGraph::primary_bars() const {
    const auto found = m_implementation->by_binding.find(m_implementation->snapshot.definition().primary_series_id);
    return found == m_implementation->by_binding.end()
               ? std::span<const Market::Core::Series::Bar>{}
               : std::span<const Market::Core::Series::Bar>{found->second->bars};
}

std::optional<double> DataGraph::indicator_value(std::string_view id, std::string_view output,
                                                 Intern::Timestamp time) const {
    const auto* value = m_implementation->indicator_value(id, output, time);
    return value ? std::optional{value->value} : std::nullopt;
}
} // namespace Didrachma::Strategy::Core
