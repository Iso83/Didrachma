#include "StrategyPanel.h"

#include "StrategyPanelLifecycle.h"
#include "StrategyPanelOverview.h"
#include "StrategyPanelWidgets.h"

#include <Didrachma/market/providers/yahoo/Interval.h>
#include <Didrachma/strategy/core/Repository.h>
#include <Didrachma/strategy/core/Validation.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <imgui.h>

namespace Didrachma::Apps::Studio {
namespace {

template <class Range, class Id, class Label>
bool select_value(const char* caption, std::string& selected, const Range& values, Id id, Label label) {
    const auto current = std::ranges::find(values, selected, id);
    const std::string preview = current == values.end() ? "Select..." : std::invoke(label, *current);
    bool changed = false;
    if (ImGui::BeginCombo(caption, preview.c_str())) {
        for (const auto& value : values) {
            const auto value_id = std::invoke(id, value);
            if (ImGui::Selectable(std::invoke(label, value).c_str(), selected == value_id)) {
                selected = value_id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void validation_at(const std::vector<Strategy::Core::RepositoryError>& errors, std::string_view path) {
    for (const auto& error : errors)
        if (error.path == path)
            ImGui::TextColored({1, .4F, .4F, 1}, "%s", error.message.c_str());
}

} // namespace

using Intern::condition_editor;
using Intern::price_policy;
using Intern::text_input;

void draw_strategies_panel(Didrachma::Studio::Core::Strategies& strategies, StrategyPanelState& state,
                           std::span<const Analysis::Core::Indicator::Definition> catalog,
                           std::function<StockChart::Core::Document&(const Market::Core::Series::Key&)> open_chart,
                           std::function<StockChart::Core::Document*(std::string_view)> find_chart) {
    if (state.historical_backtests) {
        ImGui::Begin("Historical backtests");
        for (const auto& run : state.historical_backtests->runs()) {
            ImGui::SeparatorText(run.id.c_str());
            ImGui::Text("Strategy: %s", run.request.strategy_snapshot.display_name.c_str());
            ImGui::Text("Subject: %s", run.request.subject_symbol.value_or("—").c_str());
            static constexpr const char* labels[]{"Preparing", "Loading", "Ready",    "Executing",
                                                  "Completed", "Failed",  "Cancelled"};
            ImGui::Text("Status: %s", labels[static_cast<std::size_t>(run.progress.kind)]);
            for (const auto& update : run.progress_history)
                if (!update.binding_id.empty())
                    ImGui::BulletText("%s: %s", update.binding_id.c_str(),
                                      labels[static_cast<std::size_t>(update.kind)]);
            if (run.outcome)
                for (const auto& input : run.outcome->inputs)
                    ImGui::BulletText("%s resolved to %s (%u %s)", input.series.binding_id.c_str(),
                                      input.series.key.instrument.c_str(), input.series.key.timeframe.quantity,
                                      input.series.key.timeframe.unit == Market::Core::Time::Unit::Minute ? "minute"
                                      : input.series.key.timeframe.unit == Market::Core::Time::Unit::Hour ? "hour"
                                                                                                          : "day");
            if (run.outcome)
                for (const auto& error : run.outcome->errors)
                    ImGui::TextColored({1, .4F, .4F, 1}, "Error %d at %s: %s", static_cast<int>(error.code),
                                       error.path.c_str(), error.message.c_str());
        }
        if (state.historical_backtests->busy() && ImGui::Button("Cancel historical backtest"))
            state.historical_backtests->request_cancel();
        ImGui::End();
    }
    const auto open_editor = [&](std::string_view id) {
        const auto record =
            std::ranges::find(strategies.definitions(), id, [](const auto& item) { return item.definition.id; });
        if (record == strategies.definitions().end())
            return;

        state.selected_definition = std::string{id};
        state.edited_definition = std::string{id};
        state.editor = std::make_unique<Didrachma::Studio::Core::StrategyEditor>(record->definition, record->costs);
        state.editor_open = true;
        state.validation_errors.clear();
    };
    const auto execute_transition = [&](const Didrachma::Studio::Core::DraftTransition& transition) {
        state.validation_errors.clear();
        state.messages.clear();
        using Kind = Didrachma::Studio::Core::DraftTransitionKind;
        if (transition.kind == Kind::Close)
            return;

        if (transition.kind == Kind::SwitchDefinition) {
            open_editor(transition.definition_id);
            return;
        }
        if (transition.kind == Kind::Create) {
            Strategy::Core::Definition definition;
            definition.id = "strategy-" + std::to_string(strategies.definitions().size() + 1);
            definition.display_name = "New strategy";
            definition.entry.condition = std::make_shared<Strategy::Core::ConditionExpression>();
            definition.entry.condition->id = "entry";
            open_editor(strategies.create(std::move(definition)).definition.id);
        } else if (transition.kind == Kind::Duplicate) {
            open_editor(strategies.duplicate(transition.definition_id).definition.id);
        } else if (transition.kind == Kind::Import) {
            const auto errors = strategies.import(transition.definition_id, catalog);
            state.validation_errors = errors;
            if (errors.empty()) {
                const auto imported =
                    std::ranges::find(strategies.definitions(), std::filesystem::path{transition.definition_id},
                                      [](const auto& record) { return record.path.value_or(""); });
                if (imported != strategies.definitions().end())
                    open_editor(imported->definition.id);
            }
        } else if (transition.kind == Kind::Delete) {
            strategies.erase(transition.definition_id, true);
            if (state.selected_definition == transition.definition_id)
                state.selected_definition.clear();
        }
    };
    const auto request_transition = [&](Didrachma::Studio::Core::DraftTransition transition) {
        const auto result = Didrachma::Studio::Core::request_draft_transition(
            state.draft_transition, transition, state.edited_definition, state.editor && state.editor->dirty());
        if (result == Didrachma::Studio::Core::DraftRequestResult::SameDefinition) {
            state.editor_open = true;
            return;
        }

        if (result == Didrachma::Studio::Core::DraftRequestResult::Proceed) {
            state.editor.reset();
            state.editor_open = false;
            state.edited_definition.clear();
            execute_transition(transition);
        }
    };
    Intern::draw_strategy_overview(strategies, state, catalog, request_transition, std::move(open_chart),
                                   std::move(find_chart));

    const auto editor_definition = std::ranges::find(strategies.definitions(), state.edited_definition,
                                                     [](const auto& item) { return item.definition.id; });
    if (state.editor_open && editor_definition != strategies.definitions().end()) {
        if (!state.editor)
            state.editor = std::make_unique<Didrachma::Studio::Core::StrategyEditor>(editor_definition->definition,
                                                                                     editor_definition->costs);
        bool window_open = true;
        ImGui::Begin("Strategy editor", &window_open);
        auto& definition = state.editor->draft();
        const auto draft_before_frame = Strategy::Core::serialize(definition);
        text_input("Name", definition.display_name);
        validation_at(state.validation_errors, "/displayName");
        text_input("Description", definition.description);
        if (ImGui::CollapsingHeader("Identity and instruments", ImGuiTreeNodeFlags_DefaultOpen)) {
            select_value(
                "Primary series", definition.primary_series_id, definition.series,
                [](const auto& value) { return value.id; },
                [&](const auto& value) { return state.editor->series_label(value.id); });
            validation_at(state.validation_errors, "/primarySeriesId");
            const char* directions[]{"Long", "Short"};
            auto direction = static_cast<int>(definition.direction);
            if (ImGui::Combo("Direction", &direction, directions, 2))
                definition.direction = static_cast<Strategy::Core::Direction>(direction);
        }
        if (ImGui::CollapsingHeader("Named series and timeframes")) {
            for (std::size_t series_index = 0; series_index < definition.series.size();) {
                auto& series = definition.series[series_index];
                ImGui::PushID(&series);
                text_input("Role name", series.display_name);
                const auto series_path = "/series/" + std::to_string(series_index);
                validation_at(state.validation_errors, series_path + "/displayName");
                const std::vector<Didrachma::Studio::Core::LabeledId> providers{{"yahoo", "Yahoo Finance"}};
                select_value(
                    "Provider", series.provider_id, providers, [](const auto& item) { return item.id; },
                    [](const auto& item) { return item.label; });
                validation_at(state.validation_errors, series_path + "/providerId");
                bool fixed = series.instrument.kind == Strategy::Core::InstrumentKind::Fixed;
                if (ImGui::Checkbox("Fixed instrument", &fixed))
                    series.instrument.kind =
                        fixed ? Strategy::Core::InstrumentKind::Fixed : Strategy::Core::InstrumentKind::Subject;
                if (fixed)
                    text_input("Symbol", series.instrument.symbol);
                validation_at(state.validation_errors, series_path + "/instrument/symbol");
                const auto intervals = Market::Providers::Yahoo::intervals();
                const auto current = Market::Providers::Yahoo::find_interval(series.timeframe);
                if (ImGui::BeginCombo("Timeframe", current ? current->display_name.data() : "Select...")) {
                    for (const auto& interval : intervals)
                        if (ImGui::Selectable(interval.display_name.data(), interval.frame == series.timeframe))
                            series.timeframe = interval.frame;
                    ImGui::EndCombo();
                }
                validation_at(state.validation_errors, series_path + "/timeframe");
                ImGui::Separator();
                bool removed = false;
                if (ImGui::Button("Remove series")) {
                    const auto blocked = state.editor->series_references(series.id);
                    if (!blocked)
                        removed = state.editor->remove_series(series.id);
                    else
                        state.messages = blocked.references;
                }
                ImGui::PopID();
                if (removed)
                    continue;

                ++series_index;
            }
            if (ImGui::Button("Add series"))
                state.editor->add_series();
        }
        if (ImGui::CollapsingHeader("Indicators and patterns")) {
            for (std::size_t indicator_index = 0; indicator_index < definition.indicators.size();) {
                auto& indicator = definition.indicators[indicator_index];
                ImGui::PushID(&indicator);
                text_input("Instance name", indicator.display_name);
                const auto indicator_path = "/indicators/" + std::to_string(indicator_index);
                validation_at(state.validation_errors, indicator_path + "/displayName");
                select_value(
                    "Series", indicator.series_id, definition.series, [](const auto& value) { return value.id; },
                    [&](const auto& value) { return state.editor->series_label(value.id); });
                validation_at(state.validation_errors, indicator_path + "/seriesId");
                if (select_value(
                        "Catalog definition", indicator.definition_id, catalog,
                        [](const auto& value) { return value.id; },
                        [](const auto& value) { return value.group + " / " + value.display_name; })) {
                    const auto selected_definition =
                        std::ranges::find(catalog, indicator.definition_id, &Analysis::Core::Indicator::Definition::id);
                    if (selected_definition != catalog.end())
                        state.editor->select_indicator(indicator, *selected_definition);
                }
                validation_at(state.validation_errors, indicator_path + "/definitionId");
                const auto metadata =
                    std::ranges::find(catalog, indicator.definition_id, &Analysis::Core::Indicator::Definition::id);
                for (auto& [name, parameter] : indicator.parameters) {
                    ImGui::PushID(name.c_str());
                    const char* parameter_label = name.c_str();
                    const Analysis::Core::Indicator::ParameterDefinition* parameter_definition_ptr = nullptr;
                    if (metadata != catalog.end()) {
                        const auto parameter_definition = std::ranges::find(
                            metadata->parameters, name, &Analysis::Core::Indicator::ParameterDefinition::id);
                        if (parameter_definition != metadata->parameters.end()) {
                            parameter_label = parameter_definition->display_name.c_str();
                            parameter_definition_ptr = &*parameter_definition;
                        }
                    }
                    auto edited_parameter = parameter;
                    bool parameter_changed = false;
                    std::visit(
                        [&](auto& value) {
                            using Value = std::decay_t<decltype(value)>;
                            if constexpr (std::is_same_v<Value, std::int64_t>)
                                parameter_changed = ImGui::InputScalar(parameter_label, ImGuiDataType_S64, &value);
                            else if constexpr (std::is_same_v<Value, double>)
                                parameter_changed = ImGui::InputDouble(parameter_label, &value);
                            else if constexpr (std::is_same_v<Value, bool>)
                                parameter_changed = ImGui::Checkbox(parameter_label, &value);
                            else {
                                const auto before = value;
                                text_input(name.c_str(), value);
                                parameter_changed = value != before;
                            }
                        },
                        edited_parameter);
                    if (parameter_changed && parameter_definition_ptr)
                        state.editor->set_parameter(indicator, *parameter_definition_ptr, std::move(edited_parameter));
                    if (parameter_definition_ptr &&
                        (parameter_definition_ptr->minimum || parameter_definition_ptr->maximum))
                        ImGui::TextDisabled("Range: %s%s%s",
                                            parameter_definition_ptr->minimum
                                                ? std::to_string(*parameter_definition_ptr->minimum).c_str()
                                                : "—",
                                            " to ",
                                            parameter_definition_ptr->maximum
                                                ? std::to_string(*parameter_definition_ptr->maximum).c_str()
                                                : "—");
                    validation_at(state.validation_errors, indicator_path + "/parameters/" + name);
                    ImGui::PopID();
                }
                validation_at(state.validation_errors, indicator_path + "/parameters");
                bool removed = false;
                if (ImGui::Button("Remove indicator")) {
                    const auto blocked = state.editor->indicator_references(indicator.id);
                    if (!blocked)
                        removed = state.editor->remove_indicator(indicator.id);
                    else
                        state.messages = blocked.references;
                }
                ImGui::PopID();
                if (removed)
                    continue;

                ++indicator_index;
            }
            if (ImGui::Button("Add indicator"))
                state.editor->add_indicator(catalog);
        }
        if (ImGui::CollapsingHeader("Entry", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* orders[]{"Next bar open", "Limit"};
            auto order = static_cast<int>(definition.entry.order.kind);
            if (ImGui::Combo("Order", &order, orders, 2))
                definition.entry.order.kind = static_cast<Strategy::Core::EntryOrderKind>(order);
            if (definition.entry.order.kind == Strategy::Core::EntryOrderKind::Limit) {
                ImGui::InputDouble("Limit price", &definition.entry.order.limit_price);
                validation_at(state.validation_errors, "/entry/order/limitPrice");
                ImGui::InputScalar("Validity (primary bars)", ImGuiDataType_U64,
                                   &definition.entry.order.validity_primary_bars);
                validation_at(state.validation_errors, "/entry/order/validityPrimaryBars");
            }
            validation_at(state.validation_errors, "/entry/order");
            if (ImGui::Button("Add price range preset")) {
                auto range = state.editor->make_condition(Strategy::Core::ConditionKind::All);
                auto& minimum = state.editor->add_child(*range, Strategy::Core::ConditionKind::MarketComparison);
                minimum->predicate =
                    Strategy::Core::MarketComparison{definition.primary_series_id, Strategy::Core::MarketField::Close,
                                                     Strategy::Core::Comparison::GreaterOrEqual, 0};
                auto& maximum = state.editor->add_child(*range, Strategy::Core::ConditionKind::MarketComparison);
                maximum->predicate =
                    Strategy::Core::MarketComparison{definition.primary_series_id, Strategy::Core::MarketField::Close,
                                                     Strategy::Core::Comparison::LessOrEqual, 0};
                definition.entry.condition = std::move(range);
            }
            condition_editor("Entry condition", definition.entry.condition, definition, catalog, *state.editor,
                             "/entry/condition", state.validation_errors);
            validation_at(state.validation_errors, "/entry/condition");
        }
        if (ImGui::CollapsingHeader("Initial stop and target")) {
            price_policy("Stop", definition.stop_loss, definition, catalog, *state.editor, "/stopLoss",
                         state.validation_errors);
            validation_at(state.validation_errors, "/stopLoss");
            price_policy("Target", definition.target, definition, catalog, *state.editor, "/target",
                         state.validation_errors);
            validation_at(state.validation_errors, "/target");
        }
        if (ImGui::CollapsingHeader("Runtime rules and exit conditions")) {
            for (std::size_t exit_index = 0; exit_index < definition.exits.size();) {
                const auto exit_label = "Exit " + std::to_string(exit_index + 1);
                condition_editor(exit_label.c_str(), definition.exits[exit_index].condition, definition, catalog,
                                 *state.editor, "/exits/" + std::to_string(exit_index) + "/condition",
                                 state.validation_errors);
                validation_at(state.validation_errors, "/exits/" + std::to_string(exit_index) + "/condition");
                ImGui::PushID(static_cast<int>(exit_index));
                bool removed = ImGui::Button("Remove exit") && state.editor->remove_exit(exit_index);
                ImGui::SameLine();
                if (ImGui::Button("Exit up") && exit_index > 0)
                    state.editor->move(definition.exits, exit_index, exit_index - 1);
                ImGui::SameLine();
                if (ImGui::Button("Exit down") && exit_index + 1 < definition.exits.size())
                    state.editor->move(definition.exits, exit_index, exit_index + 1);
                ImGui::PopID();
                if (!removed)
                    ++exit_index;
            }
            if (ImGui::Button("Add exit"))
                state.editor->add_exit();
            for (std::size_t rule_index = 0; rule_index < definition.runtime_rules.size();) {
                auto& rule = definition.runtime_rules[rule_index];
                ImGui::PushID(&rule);
                ImGui::SeparatorText(("Runtime rule " + std::to_string(rule_index + 1)).c_str());
                ImGui::InputInt("Priority", &rule.priority);
                condition_editor("Rule condition", rule.condition, definition, catalog, *state.editor,
                                 "/runtimeRules/" + std::to_string(rule_index) + "/condition", state.validation_errors);
                validation_at(state.validation_errors, "/runtimeRules/" + std::to_string(rule_index) + "/condition");
                for (std::size_t action_index = 0; action_index < rule.actions.size();) {
                    auto& action = rule.actions[action_index];
                    ImGui::PushID(static_cast<int>(action_index));
                    const char* actions[]{"Adjust stop", "Adjust target", "Exit"};
                    auto kind = static_cast<int>(action.kind);
                    if (ImGui::Combo("Action", &kind, actions, 3))
                        action.kind = static_cast<Strategy::Core::ActionKind>(kind);
                    if (action.kind == Strategy::Core::ActionKind::Exit) {
                        text_input("Exit reason", action.exit_reason);
                        validation_at(state.validation_errors, "/runtimeRules/" + std::to_string(rule_index) +
                                                                   "/actions/" + std::to_string(action_index) +
                                                                   "/exitReason");
                        validation_at(state.validation_errors, "/runtimeRules/" + std::to_string(rule_index) +
                                                                   "/actions/" + std::to_string(action_index) +
                                                                   "/price");
                    } else {
                        if (!action.price)
                            action.price = Strategy::Core::PricePolicy{};
                        price_policy("Action price", *action.price, definition, catalog, *state.editor,
                                     "/runtimeRules/" + std::to_string(rule_index) + "/actions/" +
                                         std::to_string(action_index) + "/price",
                                     state.validation_errors);
                    }
                    validation_at(state.validation_errors, "/runtimeRules/" + std::to_string(rule_index) + "/actions/" +
                                                               std::to_string(action_index));
                    bool removed = ImGui::Button("Remove action") && state.editor->remove_action(rule, action_index);
                    ImGui::SameLine();
                    if (ImGui::Button("Action up") && action_index > 0)
                        state.editor->move(rule.actions, action_index, action_index - 1);
                    ImGui::SameLine();
                    if (ImGui::Button("Action down") && action_index + 1 < rule.actions.size())
                        state.editor->move(rule.actions, action_index, action_index + 1);
                    ImGui::PopID();
                    if (!removed)
                        ++action_index;
                }
                if (ImGui::Button("Add action"))
                    state.editor->add_action(rule);
                bool removed = ImGui::Button("Remove rule") && state.editor->remove_rule(rule_index);
                ImGui::SameLine();
                if (ImGui::Button("Rule up") && rule_index > 0)
                    state.editor->move(definition.runtime_rules, rule_index, rule_index - 1);
                ImGui::SameLine();
                if (ImGui::Button("Rule down") && rule_index + 1 < definition.runtime_rules.size())
                    state.editor->move(definition.runtime_rules, rule_index, rule_index + 1);
                ImGui::PopID();
                if (!removed)
                    ++rule_index;
            }
            if (ImGui::Button("Add runtime rule"))
                state.editor->add_rule();
        }
        if (ImGui::CollapsingHeader("Advanced")) {
            ImGui::TextDisabled("Definition id: %s", definition.id.c_str());
            for (std::size_t index = 0; index < definition.series.size(); ++index)
                ImGui::TextDisabled("Series %zu id: %s", index + 1, definition.series[index].id.c_str());
            for (std::size_t index = 0; index < definition.indicators.size(); ++index)
                ImGui::TextDisabled("Indicator %zu id: %s", index + 1, definition.indicators[index].id.c_str());
            for (std::size_t index = 0; index < definition.exits.size(); ++index)
                ImGui::TextDisabled("Exit %zu id: %s", index + 1, definition.exits[index].id.c_str());
            for (std::size_t index = 0; index < definition.runtime_rules.size(); ++index)
                ImGui::TextDisabled("Runtime rule %zu id: %s", index + 1, definition.runtime_rules[index].id.c_str());
        }
        ImGui::Separator();
        if (Strategy::Core::serialize(definition) != draft_before_frame)
            state.editor->changed();
        if (ImGui::Button("Apply")) {
            const auto errors = strategies.apply(definition.id, *state.editor, catalog);
            state.validation_errors = errors;
            state.messages.clear();
            for (const auto& error : errors)
                state.messages.push_back(error.path + ": " + error.message);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            state.editor->cancel();
            state.editor_open = false;
            state.editor.reset();
        }
        ImGui::SameLine();
        if (editor_definition->path && ImGui::Button("Save")) {
            const auto errors = strategies.save_draft(definition.id, *state.editor, *editor_definition->path, catalog);
            state.validation_errors = errors;
            state.messages.clear();
            for (const auto& error : errors)
                state.messages.push_back(error.path + ": " + error.message);
        }
        text_input("Save as path", state.save_path);
        if (ImGui::Button("Save as") && !state.save_path.empty()) {
            const auto errors = strategies.save_draft(definition.id, *state.editor, state.save_path, catalog);
            state.validation_errors = errors;
            state.messages.clear();
            for (const auto& error : errors)
                state.messages.push_back(error.path + ": " + error.message);
        }
        for (const auto& message : state.messages)
            ImGui::TextColored({1, .4F, .4F, 1}, "%s", message.c_str());
        ImGui::End();
        if (!window_open) {
            if (state.draft_transition.request({Didrachma::Studio::Core::DraftTransitionKind::Close, {}},
                                               state.editor->dirty())) {
                state.editor_open = false;
                state.editor.reset();
                state.edited_definition.clear();
            }
        }
    }

    Intern::draw_unsaved_transition(strategies, state, catalog, execute_transition);
    draw_backtest_setup(strategies, state);
}

} // namespace Didrachma::Apps::Studio
