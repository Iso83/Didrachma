#include <Didrachma/analysis/core/indicator/Validation.h>
#include <Didrachma/strategy/core/Validation.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Didrachma::Strategy::Core::Intern {
using Errors = std::vector<ValidationError>;
void error(Errors& errors, ValidationCode code, std::string path, std::string message) {
    errors.push_back({code, std::move(path), std::move(message)});
}

template <class Entries> void unique_ids(const Entries& entries, std::string_view path, Errors& errors) {
    std::set<std::string> ids;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto item_path = std::string(path) + "/" + std::to_string(index) + "/id";
        if (entries[index].id.empty())
            error(errors, ValidationCode::MissingValue, item_path, "Id must not be empty");
        else if (!ids.insert(entries[index].id).second)
            error(errors, ValidationCode::DuplicateId, item_path, "Duplicate id: " + entries[index].id);
    }
}

bool valid_frame(Market::Core::Time::Frame frame) {
    return frame.quantity > 0 &&
           (frame.unit == Market::Core::Time::Unit::Minute || frame.unit == Market::Core::Time::Unit::Hour ||
            frame.unit == Market::Core::Time::Unit::Day);
}

const Analysis::Core::Indicator::Definition* find_definition(IndicatorCatalog catalog, const std::string& id) {
    for (const auto& definition : catalog)
        if (definition.id == id)
            return &definition;

    return nullptr;
}

bool output_exists(const Definition& definition, const std::string& indicator_id, const std::string& output_id,
                   IndicatorCatalog catalog) {
    for (const auto& binding : definition.indicators) {
        if (binding.id != indicator_id)
            continue;

        const auto* indicator = find_definition(catalog, binding.definition_id);
        if (!indicator || catalog.empty())
            return !output_id.empty();

        for (const auto& output : indicator->outputs)
            if (output.id == output_id)
                return true;
    }

    return false;
}

void validate_price(const PricePolicy& price, const Definition& definition, IndicatorCatalog catalog,
                    const std::string& path, Errors& errors) {
    if (!std::isfinite(price.value) || !std::isfinite(price.offset))
        error(errors, ValidationCode::InvalidPricePolicy, path, "Price values must be finite");
    if (price.kind == PricePolicyKind::Absolute && price.value <= 0)
        error(errors, ValidationCode::InvalidPricePolicy, path + "/value", "Absolute price must be positive");
    if (price.kind == PricePolicyKind::PercentageFromEntry && price.value < 0)
        error(errors, ValidationCode::InvalidPricePolicy, path + "/value", "Percentage must not be negative");
    if (price.kind == PricePolicyKind::IndicatorValue &&
        !output_exists(definition, price.indicator_id, price.output_id, catalog))
        error(errors, ValidationCode::MissingReference, path + "/indicatorId", "Unknown indicator output reference");
}

void validate_condition(const std::shared_ptr<ConditionExpression>& condition, const Definition& definition,
                        IndicatorCatalog catalog, const std::string& path, Errors& errors,
                        std::unordered_set<const ConditionExpression*>& visiting,
                        std::unordered_set<const ConditionExpression*>& visited, std::set<std::string>& ids) {
    if (!condition) {
        error(errors, ValidationCode::MissingValue, path, "Condition is required");
        return;
    }

    if (visiting.contains(condition.get())) {
        error(errors, ValidationCode::DependencyCycle, path, "Condition dependency cycle");
        return;
    }

    if (visited.contains(condition.get()))
        return;

    visiting.insert(condition.get());
    if (condition->id.empty())
        error(errors, ValidationCode::MissingValue, path + "/id", "Condition id is required");
    else if (!ids.insert(condition->id).second)
        error(errors, ValidationCode::DuplicateId, path + "/id", "Duplicate condition id: " + condition->id);

    const auto group = condition->kind == ConditionKind::All || condition->kind == ConditionKind::Any ||
                       condition->kind == ConditionKind::Not || condition->kind == ConditionKind::Sequence;
    if (group) {
        if (!std::holds_alternative<std::monostate>(condition->predicate))
            error(errors, ValidationCode::InvalidCondition, path + "/predicate", "Groups cannot contain a predicate");
        const auto valid_count =
            condition->kind == ConditionKind::Not ? condition->children.size() == 1 : !condition->children.empty();
        if (!valid_count)
            error(errors, ValidationCode::InvalidCondition, path + "/children", "Invalid group child count");
        if (condition->kind == ConditionKind::Sequence && condition->sequence.maximum_elapsed &&
            *condition->sequence.maximum_elapsed <= Duration::zero())
            error(errors, ValidationCode::InvalidCondition, path + "/sequence/maximumElapsedSeconds",
                  "Sequence timeout must be positive");
        if (condition->kind == ConditionKind::Sequence && condition->sequence.maximum_closed_bars &&
            *condition->sequence.maximum_closed_bars == 0)
            error(errors, ValidationCode::InvalidCondition, path + "/sequence/maximumClosedBars",
                  "Sequence bar timeout must be positive");
    } else if (!condition->children.empty()) {
        error(errors, ValidationCode::InvalidCondition, path + "/children", "Leaf conditions cannot have children");
    }

    const auto missing_indicator = [&](const std::string& id, const std::string& output) {
        if (!output_exists(definition, id, output, catalog))
            error(errors, ValidationCode::MissingReference, path + "/predicate/indicatorId",
                  "Unknown indicator output reference");
    };
    if (const auto* value = std::get_if<MarketComparison>(&condition->predicate)) {
        if (std::ranges::none_of(definition.series, [&](const auto& item) { return item.id == value->series_id; }))
            error(errors, ValidationCode::MissingReference, path + "/predicate/seriesId", "Unknown series binding");
        if (!std::isfinite(value->value))
            error(errors, ValidationCode::InvalidCondition, path + "/predicate/value", "Value must be finite");
    } else if (const auto* value = std::get_if<IndicatorComparison>(&condition->predicate)) {
        missing_indicator(value->indicator_id, value->output_id);
    } else if (const auto* value = std::get_if<IndicatorCross>(&condition->predicate)) {
        missing_indicator(value->left_indicator_id, value->left_output_id);
        if (value->right_indicator_id)
            missing_indicator(*value->right_indicator_id, value->right_output_id);
        if (value->right_indicator_id.has_value() == value->constant.has_value())
            error(errors, ValidationCode::InvalidCondition, path + "/predicate", "Cross requires one right operand");
    } else if (const auto* value = std::get_if<PatternOccurrence>(&condition->predicate)) {
        missing_indicator(value->indicator_id, "value");
    } else if (const auto* value = std::get_if<ElapsedTimeCondition>(&condition->predicate)) {
        if (value->duration < Duration::zero())
            error(errors, ValidationCode::InvalidCondition, path + "/predicate/durationSeconds",
                  "Elapsed duration cannot be negative");
    } else if (const auto* value = std::get_if<UnrealizedReturnCondition>(&condition->predicate)) {
        if (!std::isfinite(value->percentage) || value->percentage < 0)
            error(errors, ValidationCode::InvalidCondition, path + "/predicate/percentage",
                  "Return percentage must be finite and non-negative");
    } else if (!group && std::holds_alternative<std::monostate>(condition->predicate)) {
        error(errors, ValidationCode::InvalidCondition, path + "/predicate", "Leaf predicate is required");
    }

    for (std::size_t index = 0; index < condition->children.size(); ++index)
        validate_condition(condition->children[index], definition, catalog, path + "/children/" + std::to_string(index),
                           errors, visiting, visited, ids);
    visiting.erase(condition.get());
    visited.insert(condition.get());
}
} // namespace Didrachma::Strategy::Core::Intern

namespace Didrachma::Strategy::Core {
std::vector<ValidationError> validate(const Definition& definition, IndicatorCatalog catalog) {
    using namespace Intern;
    Errors errors;
    if (definition.id.empty())
        error(errors, ValidationCode::MissingValue, "/id", "Strategy id is required");
    if (definition.display_name.empty())
        error(errors, ValidationCode::MissingValue, "/displayName", "Display name is required");
    if (definition.version == 0)
        error(errors, ValidationCode::MissingValue, "/version", "Version must be positive");
    unique_ids(definition.series, "/series", errors);
    unique_ids(definition.indicators, "/indicators", errors);
    unique_ids(definition.exits, "/exits", errors);
    unique_ids(definition.runtime_rules, "/runtimeRules", errors);

    const auto series_exists = [&](const std::string& id) {
        return std::ranges::any_of(definition.series, [&](const auto& item) { return item.id == id; });
    };
    if (!series_exists(definition.primary_series_id))
        error(errors, ValidationCode::MissingReference, "/primarySeriesId", "Unknown primary series binding");
    for (std::size_t index = 0; index < definition.series.size(); ++index) {
        const auto& series = definition.series[index];
        const auto path = "/series/" + std::to_string(index);
        if (series.provider_id.empty())
            error(errors, ValidationCode::MissingValue, path + "/providerId", "Provider id is required");
        if (!valid_frame(series.timeframe))
            error(errors, ValidationCode::InvalidTimeframe, path + "/timeframe",
                  "Timeframe quantity or unit is invalid");
        if (series.instrument.kind == InstrumentKind::Fixed && series.instrument.symbol.empty())
            error(errors, ValidationCode::InvalidInstrument, path + "/instrument/symbol",
                  "Fixed instrument requires a symbol");
        if (series.instrument.kind == InstrumentKind::Subject && !series.instrument.symbol.empty())
            error(errors, ValidationCode::InvalidInstrument, path + "/instrument/symbol",
                  "Subject instrument cannot store a symbol");
        if (series.maximum_data_age && *series.maximum_data_age <= Duration::zero())
            error(errors, ValidationCode::InvalidTimeframe, path + "/maximumDataAgeSeconds",
                  "Maximum data age must be positive");
    }
    for (std::size_t index = 0; index < definition.indicators.size(); ++index) {
        const auto& indicator = definition.indicators[index];
        const auto path = "/indicators/" + std::to_string(index);
        if (!series_exists(indicator.series_id))
            error(errors, ValidationCode::MissingReference, path + "/seriesId", "Unknown series binding");
        const auto* catalog_definition = find_definition(catalog, indicator.definition_id);
        if (!catalog.empty() && !catalog_definition)
            error(errors, ValidationCode::MissingReference, path + "/definitionId", "Unknown indicator definition");
        else if (catalog_definition) {
            if (const auto invalid =
                    Analysis::Core::Indicator::validate_parameters(*catalog_definition, indicator.parameters))
                error(errors, ValidationCode::InvalidIndicatorParameter, path + "/parameters", invalid->message);
            for (const auto& [parameter_id, parameter_value] : indicator.parameters) {
                (void)parameter_value;
                if (std::ranges::none_of(catalog_definition->parameters,
                                         [&](const auto& parameter) { return parameter.id == parameter_id; }))
                    error(errors, ValidationCode::InvalidIndicatorParameter, path + "/parameters/" + parameter_id,
                          "Unknown indicator parameter");
            }
        }
    }

    const auto& order = definition.entry.order;
    if (order.kind == EntryOrderKind::Limit) {
        if (!std::isfinite(order.limit_price) || order.limit_price <= 0)
            error(errors, ValidationCode::InvalidEntryOrder, "/entry/order/limitPrice",
                  "Limit price must be finite and positive");
        if (order.validity_primary_bars == 0)
            error(errors, ValidationCode::InvalidEntryOrder, "/entry/order/validityPrimaryBars",
                  "Limit validity must be positive");
    } else if (order.limit_price != 0 || order.validity_primary_bars != 0) {
        error(errors, ValidationCode::InvalidEntryOrder, "/entry/order",
              "Next-bar-open orders cannot contain limit fields");
    }
    validate_price(definition.stop_loss, definition, catalog, "/stopLoss", errors);
    validate_price(definition.target, definition, catalog, "/target", errors);
    std::unordered_set<const ConditionExpression*> visiting, visited;
    std::set<std::string> condition_ids;
    validate_condition(definition.entry.condition, definition, catalog, "/entry/condition", errors, visiting, visited,
                       condition_ids);
    for (std::size_t index = 0; index < definition.exits.size(); ++index)
        validate_condition(definition.exits[index].condition, definition, catalog,
                           "/exits/" + std::to_string(index) + "/condition", errors, visiting, visited, condition_ids);
    for (std::size_t index = 0; index < definition.runtime_rules.size(); ++index) {
        const auto& rule = definition.runtime_rules[index];
        const auto path = "/runtimeRules/" + std::to_string(index);
        validate_condition(rule.condition, definition, catalog, path + "/condition", errors, visiting, visited,
                           condition_ids);
        if (rule.actions.empty())
            error(errors, ValidationCode::InvalidAction, path + "/actions", "Runtime rule requires an action");
        for (std::size_t action_index = 0; action_index < rule.actions.size(); ++action_index) {
            const auto& action = rule.actions[action_index];
            const auto action_path = path + "/actions/" + std::to_string(action_index);
            if (action.kind == ActionKind::Exit) {
                if (action.price)
                    error(errors, ValidationCode::InvalidAction, action_path + "/price",
                          "Exit cannot carry a price policy");
                if (action.exit_reason.empty())
                    error(errors, ValidationCode::InvalidAction, action_path + "/exitReason",
                          "Exit reason is required");
            } else if (!action.price) {
                error(errors, ValidationCode::InvalidAction, action_path + "/price",
                      "Adjustment requires a price policy");
            } else
                validate_price(*action.price, definition, catalog, action_path + "/price", errors);
        }
    }
    return errors;
}
} // namespace Didrachma::Strategy::Core
