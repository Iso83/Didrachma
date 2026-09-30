#include "StrategyEditorIntern.h"

namespace Didrachma::Studio::Core::Intern {
namespace {
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
} // namespace

Strategy::Core::Definition clone_definition(const Strategy::Core::Definition& source) {
    auto result = source;
    result.entry.condition = clone_condition(source.entry.condition);
    for (std::size_t index = 0; index < result.exits.size(); ++index)
        result.exits[index].condition = clone_condition(source.exits[index].condition);
    for (std::size_t index = 0; index < result.runtime_rules.size(); ++index)
        result.runtime_rules[index].condition = clone_condition(source.runtime_rules[index].condition);
    return result;
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

std::string frame_label(Market::Core::Time::Frame frame) {
    const char* unit = frame.unit == Market::Core::Time::Unit::Minute ? "minute"
                       : frame.unit == Market::Core::Time::Unit::Hour ? "hour"
                                                                      : "day";
    return std::to_string(frame.quantity) + " " + unit + (frame.quantity == 1 ? "" : "s");
}
} // namespace Didrachma::Studio::Core::Intern
