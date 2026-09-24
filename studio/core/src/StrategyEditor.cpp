#include <Didrachma/studio/core/StrategyEditor.h>
#include <algorithm>
#include <set>

namespace Didrachma::Studio::Core {
namespace Intern {
std::shared_ptr<Strategy::Core::ConditionExpression>
clone_condition(const std::shared_ptr<Strategy::Core::ConditionExpression>& source) {
    if (!source)
        return {};

    auto result = std::make_shared<Strategy::Core::ConditionExpression>(*source);
    result->children.clear();
    for (const auto& child : source->children)
        result->children.push_back(clone_condition(child));
    return result;
}

Strategy::Core::Definition clone_definition(const Strategy::Core::Definition& source) {
    auto result = source;
    result.entry.condition = clone_condition(source.entry.condition);
    for (std::size_t index = 0; index < result.exits.size(); ++index)
        result.exits[index].condition = clone_condition(source.exits[index].condition);
    for (std::size_t index = 0; index < result.runtime_rules.size(); ++index)
        result.runtime_rules[index].condition = clone_condition(source.runtime_rules[index].condition);
    return result;
}

void condition_references(const std::shared_ptr<Strategy::Core::ConditionExpression>& expression,
                          std::string_view series, std::string_view indicator, std::vector<std::string>& result) {
    if (!expression)
        return;

    bool match = false;
    if (const auto* value = std::get_if<Strategy::Core::MarketComparison>(&expression->predicate))
        match = !series.empty() && value->series_id == series;
    else if (const auto* value = std::get_if<Strategy::Core::IndicatorComparison>(&expression->predicate))
        match = !indicator.empty() && value->indicator_id == indicator;
    else if (const auto* value = std::get_if<Strategy::Core::IndicatorCross>(&expression->predicate))
        match = !indicator.empty() && (value->left_indicator_id == indicator ||
                                       (value->right_indicator_id && *value->right_indicator_id == indicator));
    else if (const auto* value = std::get_if<Strategy::Core::PatternOccurrence>(&expression->predicate))
        match = !indicator.empty() && value->indicator_id == indicator;
    if (match)
        result.push_back("condition " + expression->id);
    for (const auto& child : expression->children)
        condition_references(child, series, indicator, result);
}

void condition_ids(const std::shared_ptr<Strategy::Core::ConditionExpression>& expression,
                   std::set<std::string>& result) {
    if (!expression)
        return;

    result.insert(expression->id);
    for (const auto& child : expression->children)
        condition_ids(child, result);
}

void all_condition_references(const Strategy::Core::Definition& definition, std::string_view series,
                              std::string_view indicator, std::vector<std::string>& result) {
    condition_references(definition.entry.condition, series, indicator, result);
    for (const auto& exit : definition.exits)
        condition_references(exit.condition, series, indicator, result);
    for (const auto& rule : definition.runtime_rules)
        condition_references(rule.condition, series, indicator, result);
}
} // namespace Intern

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
    m_draft.series.push_back({unique_id("series"), "yahoo"});
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
    binding.parameters.clear();
    for (const auto& parameter : definition.parameters)
        binding.parameters.emplace(parameter.id, parameter.default_value);
    m_dirty = true;
}

std::vector<std::string>
StrategyEditor::compatible_indicators(std::span<const Analysis::Core::Indicator::Definition> catalog,
                                      bool patterns) const {
    std::vector<std::string> result;
    for (const auto& binding : m_draft.indicators) {
        const auto definition =
            std::ranges::find(catalog, binding.definition_id, &Analysis::Core::Indicator::Definition::id);
        const bool is_pattern = definition != catalog.end() &&
                                definition->capability == Analysis::Core::Indicator::Capability::AnalysisEvent;
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
