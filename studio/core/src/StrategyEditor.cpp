#include "StrategyEditorIntern.h"

#include <Didrachma/studio/core/StrategyEditor.h>
#include <algorithm>
#include <cmath>
#include <set>

namespace Didrachma::Studio::Core {

StrategyEditor::StrategyEditor(const Strategy::Core::Definition& definition, Strategy::Core::ExecutionCosts costs)
    : m_original(Intern::clone_definition(definition)), m_draft(Intern::clone_definition(definition)),
      m_original_costs(costs), m_draft_costs(costs) {}

std::string StrategyEditor::unique_id(std::string_view prefix) {
    std::set<std::string> ids;
    for (const auto& value : m_draft.series)
        ids.insert(value.id);
    for (const auto& value : m_draft.indicators)
        ids.insert(value.id);
    for (const auto& value : m_draft.exits)
        ids.insert(value.id);
    for (const auto& value : m_draft.runtime_rules)
        ids.insert(value.id);
    Intern::condition_ids(m_draft.entry.condition, ids);
    for (const auto& value : m_draft.exits)
        Intern::condition_ids(value.condition, ids);
    for (const auto& value : m_draft.runtime_rules)
        Intern::condition_ids(value.condition, ids);
    std::string candidate;
    do
        candidate = std::string{prefix} + "-" + std::to_string(m_next_id++);
    while (ids.contains(candidate));

    return candidate;
}

void StrategyEditor::cancel() {
    m_draft = Intern::clone_definition(m_original);
    m_draft_costs = m_original_costs;
    m_dirty = false;
}

void StrategyEditor::apply(Strategy::Core::Definition& destination, Strategy::Core::ExecutionCosts& costs) {
    apply(destination);
    costs = m_draft_costs;
    m_original_costs = m_draft_costs;
}

void StrategyEditor::apply(Strategy::Core::Definition& destination) {
    destination = Intern::clone_definition(m_draft);
    m_original = Intern::clone_definition(m_draft);
    m_dirty = false;
}

Strategy::Core::SeriesBinding& StrategyEditor::add_series() {
    const auto id = unique_id("series");
    m_draft.series.push_back({id, "yahoo"});
    m_draft.series.back().display_name = "Series " + std::to_string(m_draft.series.size());
    if (m_draft.primary_series_id.empty())
        m_draft.primary_series_id = m_draft.series.back().id;
    m_dirty = true;
    return m_draft.series.back();
}

Strategy::Core::IndicatorBinding&
StrategyEditor::add_indicator(std::span<const Analysis::Core::Indicator::Definition> catalog,
                              std::string_view definition_id) {
    m_draft.indicators.push_back({unique_id("indicator"), m_draft.primary_series_id});
    auto& result = m_draft.indicators.back();
    auto found = std::ranges::find(catalog, definition_id, &Analysis::Core::Indicator::Definition::id);
    if (found == catalog.end() && !catalog.empty())
        found = catalog.begin();
    if (found != catalog.end())
        select_indicator(result, *found);
    m_dirty = true;
    return result;
}

Strategy::Core::ExitCondition& StrategyEditor::add_exit() {
    Strategy::Core::ExitCondition value{unique_id("exit")};
    value.condition = make_condition(Strategy::Core::ConditionKind::All);
    m_draft.exits.push_back(std::move(value));
    m_dirty = true;
    return m_draft.exits.back();
}

Strategy::Core::RuntimeRule& StrategyEditor::add_rule() {
    Strategy::Core::RuntimeRule value;
    value.id = unique_id("rule");
    value.condition = make_condition(Strategy::Core::ConditionKind::All);
    m_draft.runtime_rules.push_back(std::move(value));
    m_dirty = true;
    return m_draft.runtime_rules.back();
}

Strategy::Core::RuntimeAction& StrategyEditor::add_action(Strategy::Core::RuntimeRule& rule) {
    rule.actions.push_back({Strategy::Core::ActionKind::Exit, std::nullopt, "runtime rule"});
    m_dirty = true;
    return rule.actions.back();
}

std::shared_ptr<Strategy::Core::ConditionExpression>
StrategyEditor::make_condition(Strategy::Core::ConditionKind kind) {
    auto result = std::make_shared<Strategy::Core::ConditionExpression>();
    result->id = unique_id("condition");
    result->kind = kind;
    switch (kind) {
        case Strategy::Core::ConditionKind::MarketComparison:
            result->predicate = Strategy::Core::MarketComparison{};
            break;
        case Strategy::Core::ConditionKind::IndicatorComparison:
            result->predicate = Strategy::Core::IndicatorComparison{};
            break;
        case Strategy::Core::ConditionKind::IndicatorCross:
            result->predicate = Strategy::Core::IndicatorCross{};
            break;
        case Strategy::Core::ConditionKind::PatternOccurrence:
            result->predicate = Strategy::Core::PatternOccurrence{};
            break;
        case Strategy::Core::ConditionKind::ElapsedTime:
            result->predicate = Strategy::Core::ElapsedTimeCondition{};
            break;
        case Strategy::Core::ConditionKind::ClosedBarCount:
            result->predicate = Strategy::Core::ClosedBarCountCondition{};
            break;
        case Strategy::Core::ConditionKind::UnrealizedReturn:
            result->predicate = Strategy::Core::UnrealizedReturnCondition{};
            break;
        default:
            result->predicate = std::monostate{};
    }
    if (kind == Strategy::Core::ConditionKind::Not)
        result->children.push_back(make_condition(Strategy::Core::ConditionKind::MarketComparison));
    m_dirty = true;
    return result;
}

std::shared_ptr<Strategy::Core::ConditionExpression>&
StrategyEditor::add_child(Strategy::Core::ConditionExpression& parent, Strategy::Core::ConditionKind kind) {
    if (parent.kind == Strategy::Core::ConditionKind::Not && !parent.children.empty())
        return parent.children.front();

    parent.children.push_back(make_condition(kind));
    return parent.children.back();
}

void StrategyEditor::select_indicator(Strategy::Core::IndicatorBinding& binding,
                                      const Analysis::Core::Indicator::Definition& definition) {
    binding.definition_id = definition.id;
    binding.display_name = definition.display_name.empty() ? definition.id : definition.display_name;
    binding.parameters.clear();
    for (const auto& parameter : definition.parameters)
        binding.parameters.emplace(parameter.id, parameter.default_value);
    m_dirty = true;
}

bool StrategyEditor::set_condition_kind(Strategy::Core::ConditionExpression& condition,
                                        Strategy::Core::ConditionKind kind) {
    if (condition.kind == kind)
        return false;

    condition.kind = kind;
    condition.children.clear();
    condition.sequence = {};
    switch (kind) {
        case Strategy::Core::ConditionKind::MarketComparison:
            condition.predicate = Strategy::Core::MarketComparison{};
            break;
        case Strategy::Core::ConditionKind::IndicatorComparison:
            condition.predicate = Strategy::Core::IndicatorComparison{};
            break;
        case Strategy::Core::ConditionKind::IndicatorCross:
            condition.predicate = Strategy::Core::IndicatorCross{};
            break;
        case Strategy::Core::ConditionKind::PatternOccurrence:
            condition.predicate = Strategy::Core::PatternOccurrence{};
            break;
        case Strategy::Core::ConditionKind::ElapsedTime:
            condition.predicate = Strategy::Core::ElapsedTimeCondition{};
            break;
        case Strategy::Core::ConditionKind::ClosedBarCount:
            condition.predicate = Strategy::Core::ClosedBarCountCondition{};
            break;
        case Strategy::Core::ConditionKind::UnrealizedReturn:
            condition.predicate = Strategy::Core::UnrealizedReturnCondition{};
            break;
        default:
            condition.predicate = std::monostate{};
            break;
    }
    if (kind == Strategy::Core::ConditionKind::Not)
        condition.children.push_back(make_condition(Strategy::Core::ConditionKind::MarketComparison));
    m_dirty = true;
    return true;
}

void StrategyEditor::name_series(Strategy::Core::SeriesBinding& value, std::string name) {
    value.display_name = std::move(name);
    m_dirty = true;
}

void StrategyEditor::configure_series(Strategy::Core::SeriesBinding& value, std::string provider,
                                      Strategy::Core::InstrumentSelector instrument,
                                      Market::Core::Time::Frame timeframe) {
    value.provider_id = std::move(provider);
    value.instrument = std::move(instrument);
    value.timeframe = timeframe;
    m_dirty = true;
}

void StrategyEditor::select_primary(std::string id) {
    m_draft.primary_series_id = std::move(id);
    m_dirty = true;
}

void StrategyEditor::name_indicator(Strategy::Core::IndicatorBinding& value, std::string name) {
    value.display_name = std::move(name);
    m_dirty = true;
}

void StrategyEditor::set_metadata(std::string display_name, std::string description,
                                  Strategy::Core::Direction direction) {
    m_draft.display_name = std::move(display_name);
    m_draft.description = std::move(description);
    m_draft.direction = direction;
    m_dirty = true;
}

void StrategyEditor::bind_indicator(Strategy::Core::IndicatorBinding& value, std::string series_id) {
    value.series_id = std::move(series_id);
    m_dirty = true;
}

void StrategyEditor::set_entry_condition(std::shared_ptr<Strategy::Core::ConditionExpression> condition) {
    m_draft.entry.condition = std::move(condition);
    m_dirty = true;
}

void StrategyEditor::set_predicate(Strategy::Core::ConditionExpression& value,
                                   Strategy::Core::ConditionPredicate predicate) {
    value.predicate = std::move(predicate);
    m_dirty = true;
}

void StrategyEditor::set_sequence_limits(Strategy::Core::ConditionExpression& value,
                                         std::optional<Strategy::Core::Duration> elapsed,
                                         std::optional<std::uint64_t> bars) {
    value.sequence = {elapsed, bars};
    m_dirty = true;
}

void StrategyEditor::set_entry_order(Strategy::Core::EntryOrder value) {
    m_draft.entry.order = value;
    m_dirty = true;
}

void StrategyEditor::set_stop(Strategy::Core::PricePolicy value) {
    m_draft.stop_loss = std::move(value);
    m_dirty = true;
}

void StrategyEditor::set_target(Strategy::Core::PricePolicy value) {
    m_draft.target = std::move(value);
    m_dirty = true;
}

void StrategyEditor::set_exit_condition(Strategy::Core::ExitCondition& exit,
                                        std::shared_ptr<Strategy::Core::ConditionExpression> condition) {
    exit.condition = std::move(condition);
    m_dirty = true;
}

void StrategyEditor::set_rule(Strategy::Core::RuntimeRule& rule, int priority,
                              std::shared_ptr<Strategy::Core::ConditionExpression> condition) {
    rule.priority = priority;
    rule.condition = std::move(condition);
    m_dirty = true;
}

void StrategyEditor::set_action(Strategy::Core::RuntimeAction& action, Strategy::Core::ActionKind kind,
                                std::optional<Strategy::Core::PricePolicy> price, std::string reason) {
    action = {kind, std::move(price), std::move(reason)};
    m_dirty = true;
}

bool StrategyEditor::set_parameter(Strategy::Core::IndicatorBinding& binding,
                                   const Analysis::Core::Indicator::ParameterDefinition& definition,
                                   Analysis::Core::Indicator::ParameterValue value) {
    if (definition.kind == Analysis::Core::Indicator::ParameterKind::Integer) {
        auto* number = std::get_if<std::int64_t>(&value);
        if (!number)
            return false;

        if (definition.minimum)
            *number = std::max(*number, static_cast<std::int64_t>(std::ceil(*definition.minimum)));
        if (definition.maximum)
            *number = std::min(*number, static_cast<std::int64_t>(std::floor(*definition.maximum)));
    } else if (definition.kind == Analysis::Core::Indicator::ParameterKind::Real) {
        auto* number = std::get_if<double>(&value);
        if (!number || !std::isfinite(*number))
            return false;

        if (definition.minimum)
            *number = std::max(*number, *definition.minimum);
        if (definition.maximum)
            *number = std::min(*number, *definition.maximum);
    }
    auto found = binding.parameters.find(definition.id);
    if (found != binding.parameters.end() && found->second == value)
        return false;

    binding.parameters[definition.id] = std::move(value);
    m_dirty = true;
    return true;
}

std::string StrategyEditor::series_label(std::string_view id) const {
    const auto found = std::ranges::find(m_draft.series, id, &Strategy::Core::SeriesBinding::id);
    if (found == m_draft.series.end())
        return "Missing series";

    const auto name = found->display_name.empty() ? found->id : found->display_name;
    if (found->instrument.kind == Strategy::Core::InstrumentKind::Fixed)
        return name + " — " + found->instrument.symbol + " — " + Intern::frame_label(found->timeframe);

    return name + " — " + Intern::frame_label(found->timeframe);
}

std::string StrategyEditor::indicator_label(std::string_view id) const {
    const auto found = std::ranges::find(m_draft.indicators, id, &Strategy::Core::IndicatorBinding::id);
    return found == m_draft.indicators.end() ? "Missing indicator"
                                             : (found->display_name.empty() ? found->id : found->display_name) + " — " +
                                                   series_label(found->series_id);
}

std::vector<LabeledId> StrategyEditor::series_choices() const {
    std::vector<LabeledId> result;
    for (const auto& value : m_draft.series)
        result.push_back({value.id, series_label(value.id)});
    return result;
}

std::vector<LabeledId>
StrategyEditor::compatible_indicator_choices(std::span<const Analysis::Core::Indicator::Definition> catalog,
                                             bool patterns) const {
    std::vector<LabeledId> result;
    for (const auto& id : compatible_indicators(catalog, patterns))
        result.push_back({id, indicator_label(id)});
    return result;
}

std::vector<LabeledId> StrategyEditor::compatible_output_choices(
    std::string_view indicator_id, std::span<const Analysis::Core::Indicator::Definition> catalog, bool markers) const {
    const auto binding = std::ranges::find(m_draft.indicators, indicator_id, &Strategy::Core::IndicatorBinding::id);
    if (binding == m_draft.indicators.end())
        return {};

    const auto definition =
        std::ranges::find(catalog, binding->definition_id, &Analysis::Core::Indicator::Definition::id);
    if (definition == catalog.end())
        return {};

    std::vector<LabeledId> result;
    for (const auto& output : definition->outputs)
        if (markers == (output.visual == Analysis::Core::Indicator::VisualKind::Marker))
            result.push_back({output.id, output.display_name});

    return result;
}

std::vector<std::string>
StrategyEditor::compatible_indicators(std::span<const Analysis::Core::Indicator::Definition> catalog,
                                      bool patterns) const {
    std::vector<std::string> result;
    for (const auto& binding : m_draft.indicators) {
        const auto definition =
            std::ranges::find(catalog, binding.definition_id, &Analysis::Core::Indicator::Definition::id);
        const bool is_pattern = definition != catalog.end() &&
                                (definition->capability == Analysis::Core::Indicator::Capability::AnalysisEvent ||
                                 std::ranges::any_of(definition->outputs, [](const auto& output) {
                                     return output.visual == Analysis::Core::Indicator::VisualKind::Marker;
                                 }));
        if (patterns == is_pattern)
            result.push_back(binding.id);
    }
    return result;
}

std::vector<std::string>
StrategyEditor::compatible_outputs(std::string_view indicator_id,
                                   std::span<const Analysis::Core::Indicator::Definition> catalog, bool markers) const {
    const auto binding = std::ranges::find(m_draft.indicators, indicator_id, &Strategy::Core::IndicatorBinding::id);
    if (binding == m_draft.indicators.end())
        return {};

    const auto definition =
        std::ranges::find(catalog, binding->definition_id, &Analysis::Core::Indicator::Definition::id);
    if (definition == catalog.end())
        return {};

    std::vector<std::string> result;
    for (const auto& output : definition->outputs)
        if (markers == (output.visual == Analysis::Core::Indicator::VisualKind::Marker))
            result.push_back(output.id);
    return result;
}

RemovalBlock StrategyEditor::series_references(std::string_view id) const {
    RemovalBlock result;
    if (m_draft.primary_series_id == id)
        result.references.push_back("primary series");
    for (const auto& binding : m_draft.indicators)
        if (binding.series_id == id)
            result.references.push_back("indicator " + binding.id);
    Intern::all_condition_references(m_draft, id, {}, result.references);
    return result;
}

RemovalBlock StrategyEditor::indicator_references(std::string_view id) const {
    RemovalBlock result;
    const auto price = [&](const Strategy::Core::PricePolicy& value, std::string_view name) {
        if (value.kind == Strategy::Core::PricePolicyKind::IndicatorValue && value.indicator_id == id)
            result.references.push_back(std::string{name});
    };
    price(m_draft.stop_loss, "stop price");
    price(m_draft.target, "target price");
    for (const auto& rule : m_draft.runtime_rules)
        for (const auto& action : rule.actions)
            if (action.price)
                price(*action.price, "rule " + rule.id);
    Intern::all_condition_references(m_draft, {}, id, result.references);
    return result;
}

bool StrategyEditor::remove_series(std::string_view id) {
    if (series_references(id))
        return false;

    const auto found = std::ranges::find(m_draft.series, id, &Strategy::Core::SeriesBinding::id);
    if (found == m_draft.series.end())
        return false;

    m_draft.series.erase(found);
    m_dirty = true;
    return true;
}

bool StrategyEditor::remove_indicator(std::string_view id) {
    if (indicator_references(id))
        return false;

    const auto found = std::ranges::find(m_draft.indicators, id, &Strategy::Core::IndicatorBinding::id);
    if (found == m_draft.indicators.end())
        return false;

    m_draft.indicators.erase(found);
    m_dirty = true;
    return true;
}

bool StrategyEditor::remove_child(Strategy::Core::ConditionExpression& parent, std::size_t index) {
    if (index >= parent.children.size())
        return false;

    parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(index));
    m_dirty = true;
    return true;
}

bool StrategyEditor::remove_exit(std::size_t index) {
    if (index >= m_draft.exits.size())
        return false;

    m_draft.exits.erase(m_draft.exits.begin() + static_cast<std::ptrdiff_t>(index));
    m_dirty = true;
    return true;
}

bool StrategyEditor::remove_rule(std::size_t index) {
    if (index >= m_draft.runtime_rules.size())
        return false;

    m_draft.runtime_rules.erase(m_draft.runtime_rules.begin() + static_cast<std::ptrdiff_t>(index));
    m_dirty = true;
    return true;
}

bool StrategyEditor::remove_action(Strategy::Core::RuntimeRule& rule, std::size_t index) {
    if (index >= rule.actions.size())
        return false;

    rule.actions.erase(rule.actions.begin() + static_cast<std::ptrdiff_t>(index));
    m_dirty = true;
    return true;
}
} // namespace Didrachma::Studio::Core
