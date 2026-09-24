#include "StrategyPanel.h"

#include <Didrachma/strategy/core/Validation.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <imgui.h>

namespace Didrachma::Apps::Studio {
namespace {
const char* readiness(Strategy::Core::Readiness value) {
    switch (value) {
        case Strategy::Core::Readiness::Loading:
            return "Loading";
        case Strategy::Core::Readiness::Ready:
            return "Ready";
        case Strategy::Core::Readiness::Stale:
            return "Stale";
        case Strategy::Core::Readiness::InsufficientHistory:
            return "Insufficient history";
        case Strategy::Core::Readiness::ProviderError:
            return "Provider error";
        case Strategy::Core::Readiness::CalculationError:
            return "Calculation error";
    }
    return "Unknown";
}

const char* run_state(Strategy::Core::RunState value) {
    switch (value) {
        case Strategy::Core::RunState::WaitingForEntry:
            return "Waiting for entry";
        case Strategy::Core::RunState::EntryArmed:
            return "Entry armed";
        case Strategy::Core::RunState::Running:
            return "Running";
        case Strategy::Core::RunState::Exited:
            return "Exited";
        case Strategy::Core::RunState::Stopped:
            return "Stopped";
        case Strategy::Core::RunState::Error:
            return "Error";
    }
    return "Unknown";
}

void text_input(const char* label, std::string& value) {
    char buffer[256]{};
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (ImGui::InputText(label, buffer, sizeof(buffer)))
        value = buffer;
}

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

void price_policy(const char* label, Strategy::Core::PricePolicy& policy, const Strategy::Core::Definition& definition,
                  std::span<const Analysis::Core::Indicator::Definition> catalog) {
    ImGui::PushID(label);
    ImGui::SeparatorText(label);
    const char* kinds[]{"Absolute price", "Percentage from entry", "Indicator value"};
    auto kind = static_cast<int>(policy.kind);
    if (ImGui::Combo("Policy", &kind, kinds, 3))
        policy.kind = static_cast<Strategy::Core::PricePolicyKind>(kind);
    if (policy.kind == Strategy::Core::PricePolicyKind::IndicatorValue) {
        select_value(
            "Indicator binding", policy.indicator_id, definition.indicators, [](const auto& item) { return item.id; },
            [](const auto& item) { return item.id; });
        const auto binding =
            std::ranges::find(definition.indicators, policy.indicator_id, &Strategy::Core::IndicatorBinding::id);
        if (binding != definition.indicators.end()) {
            const auto metadata =
                std::ranges::find(catalog, binding->definition_id, &Analysis::Core::Indicator::Definition::id);
            if (metadata != catalog.end())
                select_value(
                    "Output", policy.output_id, metadata->outputs, [](const auto& item) { return item.id; },
                    [](const auto& item) { return item.display_name; });
        }
        ImGui::InputDouble("Offset", &policy.offset);
    } else
        ImGui::InputDouble(policy.kind == Strategy::Core::PricePolicyKind::Absolute ? "Price" : "Percentage",
                           &policy.value);
    ImGui::PopID();
}

void condition_editor(const char* label, std::shared_ptr<Strategy::Core::ConditionExpression>& condition,
                      const Strategy::Core::Definition& definition,
                      std::span<const Analysis::Core::Indicator::Definition> catalog,
                      Didrachma::Studio::Core::StrategyEditor& editor, int depth = 0) {
    if (!condition)
        condition = std::make_shared<Strategy::Core::ConditionExpression>();
    ImGui::PushID(condition.get());
    if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Condition id: %s", condition->id.c_str());
        const char* kinds[]{"Market comparison",
                            "Indicator comparison",
                            "Indicator cross",
                            "Pattern occurrence",
                            "Elapsed time",
                            "Closed bar count",
                            "Unrealized return",
                            "All",
                            "Any",
                            "Not",
                            "Sequence"};
        auto kind = static_cast<int>(condition->kind);
        if (ImGui::Combo("Kind", &kind, kinds, 11)) {
            condition->kind = static_cast<Strategy::Core::ConditionKind>(kind);
            condition->predicate = std::monostate{};
            condition->children.clear();
        }
        if (condition->kind == Strategy::Core::ConditionKind::MarketComparison) {
            if (!std::holds_alternative<Strategy::Core::MarketComparison>(condition->predicate))
                condition->predicate = Strategy::Core::MarketComparison{};
            auto& value = std::get<Strategy::Core::MarketComparison>(condition->predicate);
            select_value(
                "Series", value.series_id, definition.series, [](const auto& item) { return item.id; },
                [](const auto& item) { return item.id; });
            const char* fields[]{"Open", "High", "Low", "Close", "Volume"};
            auto field = static_cast<int>(value.field);
            if (ImGui::Combo("Market field", &field, fields, 5))
                value.field = static_cast<Strategy::Core::MarketField>(field);
            const char* comparisons[]{"Less", "Less or equal", "Equal", "Greater or equal", "Greater"};
            auto comparison = static_cast<int>(value.comparison);
            if (ImGui::Combo("Comparison", &comparison, comparisons, 5))
                value.comparison = static_cast<Strategy::Core::Comparison>(comparison);
            ImGui::InputDouble("Value", &value.value);
        } else if (condition->kind == Strategy::Core::ConditionKind::IndicatorComparison) {
            if (!std::holds_alternative<Strategy::Core::IndicatorComparison>(condition->predicate))
                condition->predicate = Strategy::Core::IndicatorComparison{};
            auto& value = std::get<Strategy::Core::IndicatorComparison>(condition->predicate);
            select_value(
                "Indicator", value.indicator_id, definition.indicators, [](const auto& item) { return item.id; },
                [](const auto& item) { return item.id; });
            const auto binding =
                std::ranges::find(definition.indicators, value.indicator_id, &Strategy::Core::IndicatorBinding::id);
            if (binding != definition.indicators.end()) {
                const auto metadata =
                    std::ranges::find(catalog, binding->definition_id, &Analysis::Core::Indicator::Definition::id);
                if (metadata != catalog.end())
                    select_value(
                        "Output", value.output_id, metadata->outputs, [](const auto& item) { return item.id; },
                        [](const auto& item) { return item.display_name; });
            }
            const char* comparisons[]{"Less", "Less or equal", "Equal", "Greater or equal", "Greater"};
            auto comparison = static_cast<int>(value.comparison);
            if (ImGui::Combo("Comparison", &comparison, comparisons, 5))
                value.comparison = static_cast<Strategy::Core::Comparison>(comparison);
            ImGui::InputDouble("Value", &value.value);
        } else if (condition->kind == Strategy::Core::ConditionKind::IndicatorCross) {
            if (!std::holds_alternative<Strategy::Core::IndicatorCross>(condition->predicate))
                condition->predicate = Strategy::Core::IndicatorCross{};
            auto& value = std::get<Strategy::Core::IndicatorCross>(condition->predicate);
            select_value(
                "Left indicator", value.left_indicator_id, definition.indicators,
                [](const auto& item) { return item.id; }, [](const auto& item) { return item.id; });
            const auto left = std::ranges::find(definition.indicators, value.left_indicator_id,
                                                &Strategy::Core::IndicatorBinding::id);
            if (left != definition.indicators.end()) {
                const auto metadata =
                    std::ranges::find(catalog, left->definition_id, &Analysis::Core::Indicator::Definition::id);
                if (metadata != catalog.end())
                    select_value(
                        "Left output", value.left_output_id, metadata->outputs,
                        [](const auto& item) { return item.id; }, [](const auto& item) { return item.display_name; });
            }
            bool constant = !value.right_indicator_id;
            if (ImGui::Checkbox("Compare with constant", &constant)) {
                value.right_indicator_id = constant ? std::nullopt : std::optional<std::string>{};
                value.constant = constant ? std::optional<double>{0} : std::nullopt;
            }
            if (constant) {
                if (!value.constant)
                    value.constant = 0.0;
                ImGui::InputDouble("Constant", &*value.constant);
            } else {
                select_value(
                    "Right indicator", *value.right_indicator_id, definition.indicators,
                    [](const auto& item) { return item.id; }, [](const auto& item) { return item.id; });
                const auto right = std::ranges::find(definition.indicators, *value.right_indicator_id,
                                                     &Strategy::Core::IndicatorBinding::id);
                if (right != definition.indicators.end()) {
                    const auto metadata =
                        std::ranges::find(catalog, right->definition_id, &Analysis::Core::Indicator::Definition::id);
                    if (metadata != catalog.end())
                        select_value(
                            "Right output", value.right_output_id, metadata->outputs,
                            [](const auto& item) { return item.id; },
                            [](const auto& item) { return item.display_name; });
                }
            }
            const char* directions[]{"Above", "Below", "Either"};
            auto direction = static_cast<int>(value.direction);
            if (ImGui::Combo("Cross direction", &direction, directions, 3))
                value.direction = static_cast<Strategy::Core::CrossDirection>(direction);
        } else if (condition->kind == Strategy::Core::ConditionKind::PatternOccurrence) {
            if (!std::holds_alternative<Strategy::Core::PatternOccurrence>(condition->predicate))
                condition->predicate = Strategy::Core::PatternOccurrence{};
            auto& value = std::get<Strategy::Core::PatternOccurrence>(condition->predicate);
            select_value(
                "Pattern", value.indicator_id, definition.indicators, [](const auto& item) { return item.id; },
                [](const auto& item) { return item.id; });
            const char* directions[]{"Any", "Upward", "Downward"};
            int direction = value.direction ? (*value.direction == Strategy::Core::Direction::Long ? 1 : 2) : 0;
            if (ImGui::Combo("Direction", &direction, directions, 3))
                value.direction = direction == 0 ? std::nullopt
                                                 : std::optional{direction == 1 ? Strategy::Core::Direction::Long
                                                                                : Strategy::Core::Direction::Short};
        } else if (condition->kind == Strategy::Core::ConditionKind::ElapsedTime) {
            if (!std::holds_alternative<Strategy::Core::ElapsedTimeCondition>(condition->predicate))
                condition->predicate = Strategy::Core::ElapsedTimeCondition{};
            auto& seconds = std::get<Strategy::Core::ElapsedTimeCondition>(condition->predicate).duration;
            auto count = seconds.count();
            if (ImGui::InputScalar("Seconds", ImGuiDataType_S64, &count))
                seconds = Strategy::Core::Duration{count};
            const char* comparisons[]{"Less", "Less or equal", "Equal", "Greater or equal", "Greater"};
            auto comparison =
                static_cast<int>(std::get<Strategy::Core::ElapsedTimeCondition>(condition->predicate).comparison);
            if (ImGui::Combo("Comparison", &comparison, comparisons, 5))
                std::get<Strategy::Core::ElapsedTimeCondition>(condition->predicate).comparison =
                    static_cast<Strategy::Core::Comparison>(comparison);
        } else if (condition->kind == Strategy::Core::ConditionKind::ClosedBarCount) {
            if (!std::holds_alternative<Strategy::Core::ClosedBarCountCondition>(condition->predicate))
                condition->predicate = Strategy::Core::ClosedBarCountCondition{};
            auto& count = std::get<Strategy::Core::ClosedBarCountCondition>(condition->predicate).count;
            ImGui::InputScalar("Bars", ImGuiDataType_U64, &count);
            const char* comparisons[]{"Less", "Less or equal", "Equal", "Greater or equal", "Greater"};
            auto comparison =
                static_cast<int>(std::get<Strategy::Core::ClosedBarCountCondition>(condition->predicate).comparison);
            if (ImGui::Combo("Comparison", &comparison, comparisons, 5))
                std::get<Strategy::Core::ClosedBarCountCondition>(condition->predicate).comparison =
                    static_cast<Strategy::Core::Comparison>(comparison);
        } else if (condition->kind == Strategy::Core::ConditionKind::UnrealizedReturn) {
            if (!std::holds_alternative<Strategy::Core::UnrealizedReturnCondition>(condition->predicate))
                condition->predicate = Strategy::Core::UnrealizedReturnCondition{};
            auto& value = std::get<Strategy::Core::UnrealizedReturnCondition>(condition->predicate);
            const char* kinds[]{"Gain", "Loss"};
            auto kind = static_cast<int>(value.kind);
            if (ImGui::Combo("Return kind", &kind, kinds, 2))
                value.kind = static_cast<Strategy::Core::ReturnKind>(kind);
            const char* comparisons[]{"Less", "Less or equal", "Equal", "Greater or equal", "Greater"};
            auto comparison = static_cast<int>(value.comparison);
            if (ImGui::Combo("Comparison", &comparison, comparisons, 5))
                value.comparison = static_cast<Strategy::Core::Comparison>(comparison);
            ImGui::InputDouble("Percentage", &value.percentage);
        } else if (condition->kind == Strategy::Core::ConditionKind::All ||
                   condition->kind == Strategy::Core::ConditionKind::Any ||
                   condition->kind == Strategy::Core::ConditionKind::Not ||
                   condition->kind == Strategy::Core::ConditionKind::Sequence) {
            for (std::size_t index = 0; index < condition->children.size();) {
                condition_editor(("Step " + std::to_string(index + 1)).c_str(), condition->children[index], definition,
                                 catalog, editor, depth + 1);
                ImGui::PushID(static_cast<int>(index));
                bool removed = false;
                if (ImGui::SmallButton("Remove"))
                    removed = editor.remove_child(*condition, index);
                ImGui::SameLine();
                if (ImGui::SmallButton("Up") && index > 0)
                    editor.move(condition->children, index, index - 1);
                ImGui::SameLine();
                if (ImGui::SmallButton("Down") && index + 1 < condition->children.size())
                    editor.move(condition->children, index, index + 1);
                ImGui::PopID();
                if (!removed)
                    ++index;
            }
            if (depth < 8 && (condition->kind != Strategy::Core::ConditionKind::Not || condition->children.empty()) &&
                ImGui::Button("Add child"))
                editor.add_child(*condition);
            if (condition->kind == Strategy::Core::ConditionKind::Sequence) {
                bool time_limited = condition->sequence.maximum_elapsed.has_value();
                if (ImGui::Checkbox("Maximum elapsed", &time_limited))
                    condition->sequence.maximum_elapsed =
                        time_limited ? std::optional<Strategy::Core::Duration>{std::chrono::minutes{1}} : std::nullopt;
                if (time_limited) {
                    auto seconds = condition->sequence.maximum_elapsed->count();
                    if (ImGui::InputScalar("Elapsed seconds", ImGuiDataType_S64, &seconds))
                        condition->sequence.maximum_elapsed = Strategy::Core::Duration{seconds};
                }
                bool limited = condition->sequence.maximum_closed_bars.has_value();
                if (ImGui::Checkbox("Maximum closed bars", &limited))
                    condition->sequence.maximum_closed_bars = limited ? std::optional<std::uint64_t>{1} : std::nullopt;
                if (limited)
                    ImGui::InputScalar("Bar limit", ImGuiDataType_U64, &*condition->sequence.maximum_closed_bars);
            }
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}
} // namespace

void draw_strategies_panel(Didrachma::Studio::Core::Strategies& strategies, StrategyPanelState& state,
                           std::span<const Analysis::Core::Indicator::Definition> catalog,
                           std::function<StockChart::Core::Document&(const Market::Core::Series::Key&)> open_chart,
                           std::function<StockChart::Core::Document*(std::string_view)> find_chart) {
    if (!ImGui::Begin("Strategies")) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("strategy-views")) {
        if (ImGui::BeginTabItem("Definitions")) {
            for (const auto& record : strategies.definitions())
                if (ImGui::Selectable(record.definition.display_name.c_str(),
                                      state.selected_definition == record.definition.id))
                    state.selected_definition = record.definition.id;
            text_input("Import from", state.import_path);
            if (ImGui::Button("Create")) {
                Strategy::Core::Definition definition;
                definition.id = "strategy-" + std::to_string(strategies.definitions().size() + 1);
                definition.display_name = "New strategy";
                definition.entry.condition = std::make_shared<Strategy::Core::ConditionExpression>();
                definition.entry.condition->id = "entry";
                state.selected_definition = strategies.create(std::move(definition)).definition.id;
                const auto created = std::ranges::find(strategies.definitions(), state.selected_definition,
                                                       [](const auto& item) { return item.definition.id; });
                state.editor =
                    std::make_unique<Didrachma::Studio::Core::StrategyEditor>(created->definition, created->costs);
                state.editor_open = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Load / import")) {
                if (strategies.import(state.import_path, catalog).empty() && !strategies.definitions().empty())
                    state.selected_definition = strategies.definitions().back().definition.id;
            }
            const auto selected = std::ranges::find(strategies.definitions(), state.selected_definition,
                                                    [](const auto& item) { return item.definition.id; });
            ImGui::SameLine();
            if (ImGui::Button("Duplicate") && !state.selected_definition.empty())
                state.selected_definition = strategies.duplicate(state.selected_definition).definition.id;
            ImGui::SameLine();
            if (ImGui::Button("Edit") && !state.selected_definition.empty()) {
                if (selected != strategies.definitions().end())
                    state.editor = std::make_unique<Didrachma::Studio::Core::StrategyEditor>(selected->definition,
                                                                                             selected->costs);
                state.editor_open = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete") && !state.selected_definition.empty())
                state.confirm_delete = true;
            text_input("Subject", state.subject);
            const bool needs_subject = selected != strategies.definitions().end() &&
                                       std::ranges::any_of(selected->definition.series, [](const auto& series) {
                                           return series.instrument.kind == Strategy::Core::InstrumentKind::Subject;
                                       });
            const bool valid = selected != strategies.definitions().end() &&
                               Strategy::Core::validate(selected->definition, catalog).empty() &&
                               (!needs_subject || !state.subject.empty());
            ImGui::BeginDisabled(!valid);
            if (ImGui::Button("Start")) {
                if (auto* run = strategies.start(state.selected_definition,
                                                 state.subject.empty() ? std::nullopt : std::optional{state.subject},
                                                 catalog, std::move(open_chart)))
                    state.selected_run = run->id;
            }
            ImGui::EndDisabled();
            if (!valid && selected != strategies.definitions().end())
                for (const auto& error : Strategy::Core::validate(selected->definition, catalog))
                    ImGui::TextColored({1, .4F, .4F, 1}, "%s: %s", error.path.c_str(), error.message.c_str());
            if (needs_subject && state.subject.empty())
                ImGui::TextColored({1, .4F, .4F, 1}, "Subject: enter a symbol before starting");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Running")) {
            for (const auto& run : strategies.runs())
                if (ImGui::Selectable(run.id.c_str(), state.selected_run == run.id))
                    state.selected_run = run.id;
            if (ImGui::Button("Stop selected") && !state.selected_run.empty())
                state.confirm_stop = true;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (state.confirm_delete) {
        ImGui::OpenPopup("Delete strategy?");
        state.confirm_delete = false;
    }
    if (ImGui::BeginPopupModal("Delete strategy?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Unsaved changes will be lost.");
        if (ImGui::Button("Delete")) {
            strategies.erase(state.selected_definition, true);
            state.selected_definition.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (state.confirm_stop) {
        ImGui::OpenPopup("Stop strategy run?");
        state.confirm_stop = false;
    }
    if (ImGui::BeginPopupModal("Stop strategy run?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::Button("Stop")) {
            strategies.stop(state.selected_run,
                            std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now()),
                            std::move(find_chart));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();

    const auto editor_definition = std::ranges::find(strategies.definitions(), state.selected_definition,
                                                     [](const auto& item) { return item.definition.id; });
    if (state.editor_open && editor_definition != strategies.definitions().end()) {
        if (!state.editor)
            state.editor = std::make_unique<Didrachma::Studio::Core::StrategyEditor>(editor_definition->definition,
                                                                                     editor_definition->costs);
        ImGui::Begin("Strategy editor", &state.editor_open);
        auto& definition = state.editor->draft();
        text_input("Name", definition.display_name);
        text_input("Description", definition.description);
        if (ImGui::CollapsingHeader("Identity and instruments", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Stable id: %s", definition.id.c_str());
            select_value(
                "Primary series", definition.primary_series_id, definition.series,
                [](const auto& value) { return value.id; }, [](const auto& value) { return value.id; });
            const char* directions[]{"Long", "Short"};
            auto direction = static_cast<int>(definition.direction);
            if (ImGui::Combo("Direction", &direction, directions, 2))
                definition.direction = static_cast<Strategy::Core::Direction>(direction);
        }
        if (ImGui::CollapsingHeader("Named series and timeframes")) {
            for (std::size_t series_index = 0; series_index < definition.series.size();) {
                auto& series = definition.series[series_index];
                ImGui::PushID(&series);
                text_input("Alias", series.id);
                text_input("Provider", series.provider_id);
                bool fixed = series.instrument.kind == Strategy::Core::InstrumentKind::Fixed;
                if (ImGui::Checkbox("Fixed instrument", &fixed))
                    series.instrument.kind =
                        fixed ? Strategy::Core::InstrumentKind::Fixed : Strategy::Core::InstrumentKind::Subject;
                if (fixed)
                    text_input("Symbol", series.instrument.symbol);
                int quantity = static_cast<int>(series.timeframe.quantity);
                if (ImGui::InputInt("Timeframe quantity", &quantity))
                    series.timeframe.quantity = static_cast<std::uint32_t>(std::max(0, quantity));
                const char* units[]{"Minute", "Hour", "Day"};
                auto unit = static_cast<int>(series.timeframe.unit);
                if (ImGui::Combo("Unit", &unit, units, 3))
                    series.timeframe.unit = static_cast<Market::Core::Time::Unit>(unit);
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
                ImGui::TextDisabled("Binding id: %s", indicator.id.c_str());
                select_value(
                    "Series", indicator.series_id, definition.series, [](const auto& value) { return value.id; },
                    [](const auto& value) { return value.id; });
                if (select_value(
                        "Catalog definition", indicator.definition_id, catalog,
                        [](const auto& value) { return value.id; },
                        [](const auto& value) { return value.group + " / " + value.display_name; })) {
                    const auto selected_definition =
                        std::ranges::find(catalog, indicator.definition_id, &Analysis::Core::Indicator::Definition::id);
                    if (selected_definition != catalog.end())
                        state.editor->select_indicator(indicator, *selected_definition);
                }
                const auto metadata =
                    std::ranges::find(catalog, indicator.definition_id, &Analysis::Core::Indicator::Definition::id);
                for (auto& [name, parameter] : indicator.parameters) {
                    ImGui::PushID(name.c_str());
                    const char* parameter_label = name.c_str();
                    if (metadata != catalog.end()) {
                        const auto parameter_definition = std::ranges::find(
                            metadata->parameters, name, &Analysis::Core::Indicator::ParameterDefinition::id);
                        if (parameter_definition != metadata->parameters.end())
                            parameter_label = parameter_definition->display_name.c_str();
                    }
                    std::visit(
                        [&](auto& value) {
                            using Value = std::decay_t<decltype(value)>;
                            if constexpr (std::is_same_v<Value, std::int64_t>)
                                ImGui::InputScalar(parameter_label, ImGuiDataType_S64, &value);
                            else if constexpr (std::is_same_v<Value, double>)
                                ImGui::InputDouble(parameter_label, &value);
                            else if constexpr (std::is_same_v<Value, bool>)
                                ImGui::Checkbox(parameter_label, &value);
                            else
                                text_input(name.c_str(), value);
                        },
                        parameter);
                    ImGui::PopID();
                }
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
        if (ImGui::CollapsingHeader("Entry expression / sequence"))
            condition_editor("Entry", definition.entry.condition, definition, catalog, *state.editor);
        if (ImGui::CollapsingHeader("Initial stop and target")) {
            price_policy("Stop", definition.stop_loss, definition, catalog);
            price_policy("Target", definition.target, definition, catalog);
        }
        if (ImGui::CollapsingHeader("Runtime rules and exit conditions")) {
            for (std::size_t exit_index = 0; exit_index < definition.exits.size();) {
                condition_editor(definition.exits[exit_index].id.c_str(), definition.exits[exit_index].condition,
                                 definition, catalog, *state.editor);
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
                ImGui::TextDisabled("Rule id: %s", rule.id.c_str());
                ImGui::InputInt("Priority", &rule.priority);
                condition_editor("Rule condition", rule.condition, definition, catalog, *state.editor);
                for (std::size_t action_index = 0; action_index < rule.actions.size();) {
                    auto& action = rule.actions[action_index];
                    ImGui::PushID(static_cast<int>(action_index));
                    const char* actions[]{"Adjust stop", "Adjust target", "Exit"};
                    auto kind = static_cast<int>(action.kind);
                    if (ImGui::Combo("Action", &kind, actions, 3))
                        action.kind = static_cast<Strategy::Core::ActionKind>(kind);
                    if (action.kind == Strategy::Core::ActionKind::Exit)
                        text_input("Exit reason", action.exit_reason);
                    else {
                        if (!action.price)
                            action.price = Strategy::Core::PricePolicy{};
                        price_policy("Action price", *action.price, definition, catalog);
                    }
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
        if (ImGui::CollapsingHeader("Position and cost assumptions")) {
            auto& costs = state.editor->draft_costs();
            ImGui::InputDouble("Quantity", &costs.quantity);
            bool has_capital = costs.starting_capital.has_value();
            if (ImGui::Checkbox("Starting capital", &has_capital))
                costs.starting_capital = has_capital ? std::optional{10000.0} : std::nullopt;
            if (costs.starting_capital)
                ImGui::InputDouble("Capital", &*costs.starting_capital);
            ImGui::InputDouble("Fixed cost per fill", &costs.fixed_per_fill);
            ImGui::InputDouble("Cost percentage per fill", &costs.percentage_per_fill);
            ImGui::InputDouble("Slippage percentage", &costs.slippage_percentage);
        }
        ImGui::Separator();
        if (ImGui::IsItemDeactivatedAfterEdit())
            state.editor->changed();
        if (ImGui::Button("Apply")) {
            state.editor->apply(editor_definition->definition, editor_definition->costs);
            editor_definition->unsaved = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            state.editor->cancel();
            state.editor_open = false;
            state.editor.reset();
        }
        ImGui::SameLine();
        if (editor_definition->path && ImGui::Button("Save")) {
            state.editor->apply(editor_definition->definition, editor_definition->costs);
            const auto errors = strategies.save(definition.id, *editor_definition->path, catalog);
            state.messages.clear();
            for (const auto& error : errors)
                state.messages.push_back(error.path + ": " + error.message);
        }
        text_input("Save as path", state.save_path);
        if (ImGui::Button("Save as") && !state.save_path.empty()) {
            state.editor->apply(editor_definition->definition, editor_definition->costs);
            const auto errors = strategies.save(definition.id, state.save_path, catalog);
            state.messages.clear();
            for (const auto& error : errors)
                state.messages.push_back(error.path + ": " + error.message);
        }
        for (const auto& message : state.messages)
            ImGui::TextColored({1, .4F, .4F, 1}, "%s", message.c_str());
        ImGui::End();
    }
}

void draw_strategy_monitor(
    Didrachma::Studio::Core::Strategies& strategies, StrategyPanelState& state,
    std::function<void(std::string_view, std::optional<Market::Core::Time::UtcTimestamp>)> navigate) {
    ImGui::Begin("Strategy monitor");
    const auto run =
        std::ranges::find(strategies.runs(), state.selected_run, &Didrachma::Studio::Core::StrategyRunMonitor::id);
    if (run == strategies.runs().end()) {
        const auto definition = std::ranges::find(strategies.definitions(), state.selected_definition,
                                                  [](const auto& value) { return value.definition.id; });
        if (definition == strategies.definitions().end())
            ImGui::TextDisabled("Select a strategy definition or run");
        else {
            ImGui::Text("%s", definition->definition.display_name.c_str());
            ImGui::TextWrapped("%s", definition->definition.description.c_str());
            ImGui::Text("Direction: %s",
                        definition->definition.direction == Strategy::Core::Direction::Long ? "Long" : "Short");
            ImGui::SeparatorText("Required series");
            for (const auto& series : definition->definition.series)
                ImGui::BulletText("%s — %s (%u %s)", series.id.c_str(),
                                  series.instrument.kind == Strategy::Core::InstrumentKind::Subject
                                      ? "subject"
                                      : series.instrument.symbol.c_str(),
                                  series.timeframe.quantity,
                                  series.timeframe.unit == Market::Core::Time::Unit::Minute ? "minute"
                                  : series.timeframe.unit == Market::Core::Time::Unit::Hour ? "hour"
                                                                                            : "day");
        }
        ImGui::End();
        return;
    }
    ImGui::Text("Run %s — %s", run->id.c_str(), run_state(run->state));
    ImGui::Text("P/L %.2f  ROI %.2f%%  duration %llds", run->result.unrealized_profit_loss,
                run->result.unrealized_return_percentage, static_cast<long long>(run->result.elapsed.count()));
    ImGui::Text("Entry %.4f  Current %.4f", run->result.entry_price.value_or(0.0),
                run->result.current_price.value_or(0.0));
    const auto last_price = [&](Strategy::Core::ProjectionKind kind) -> std::optional<double> {
        const auto found =
            std::find_if(run->result.projection.segments.rbegin(), run->result.projection.segments.rend(),
                         [&](const auto& segment) { return segment.kind == kind; });
        return found == run->result.projection.segments.rend() ? std::nullopt : std::optional{found->price};
    };
    ImGui::Text("Stop %.4f  Target %.4f", last_price(Strategy::Core::ProjectionKind::StopPrice).value_or(0.0),
                last_price(Strategy::Core::ProjectionKind::TargetPrice).value_or(0.0));
    if (!run->result.events.empty()) {
        const auto& event = run->result.events.back();
        ImGui::Text("Last rule/event: %s", event.detail.c_str());
    }
    for (const auto& series : run->series) {
        ImGui::SeparatorText(series.binding_id.c_str());
        ImGui::PushID(series.binding_id.c_str());
        if (ImGui::Button("Open chart"))
            navigate(series.chart_id, series.last_closed);
        ImGui::Text("%s / %s (%d %d): %s", series.key.provider.c_str(), series.key.instrument.c_str(),
                    series.key.timeframe.quantity, static_cast<int>(series.key.timeframe.unit),
                    readiness(series.state.readiness));
        if (series.last_closed)
            ImGui::Text("Last closed UTC epoch: %lld",
                        static_cast<long long>(series.last_closed->time_since_epoch().count()));
        for (const auto& condition : series.condition_ids)
            ImGui::BulletText("%s: Unknown (waiting for closed data)", condition.c_str());
        ImGui::PopID();
    }
    ImGui::SeparatorText("Latest condition evidence");
    for (const auto& evidence : run->evidence) {
        const char* truth = evidence.truth == Strategy::Core::Truth::True    ? "True"
                            : evidence.truth == Strategy::Core::Truth::False ? "False"
                                                                             : "Unknown";
        ImGui::BulletText("%s: %s — %s", evidence.condition_id.c_str(), truth, evidence.detail.c_str());
    }
    ImGui::SeparatorText("Event history");
    for (std::size_t index = 0; index < run->result.events.size(); ++index) {
        const auto& event = run->result.events[index];
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::Selectable(event.detail.c_str())) {
            const auto primary = std::ranges::find(run->series, run->snapshot.definition().primary_series_id,
                                                   &Didrachma::Studio::Core::StrategySeriesStatus::binding_id);
            if (primary != run->series.end())
                navigate(primary->chart_id, event.effective_time);
        }
        ImGui::PopID();
    }
    ImGui::End();
}
} // namespace Didrachma::Apps::Studio
