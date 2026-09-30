#include "StrategyPanelWidgets.h"

#include <algorithm>
#include <cstdio>
#include <imgui.h>

namespace Didrachma::Apps::Studio::Intern {
namespace {
void validation_at(const std::vector<Strategy::Core::RepositoryError>& errors, std::string_view path) {
    for (const auto& error : errors)
        if (error.path == path)
            ImGui::TextColored({1, .4F, .4F, 1}, "%s", error.message.c_str());
}
} // namespace
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
                  std::span<const Analysis::Core::Indicator::Definition> catalog,
                  Didrachma::Studio::Core::StrategyEditor& editor, std::string_view path,
                  const std::vector<Strategy::Core::RepositoryError>& errors) {
    ImGui::PushID(label);
    ImGui::SeparatorText(label);
    const char* kinds[]{"Absolute price", "Percentage from entry", "Indicator value"};
    auto kind = static_cast<int>(policy.kind);
    if (ImGui::Combo("Policy", &kind, kinds, 3)) {
        policy.kind = static_cast<Strategy::Core::PricePolicyKind>(kind);
        if (policy.kind != Strategy::Core::PricePolicyKind::IndicatorValue) {
            policy.indicator_id.clear();
            policy.output_id.clear();
        }
    }
    if (policy.kind == Strategy::Core::PricePolicyKind::IndicatorValue) {
        const auto indicators = editor.compatible_indicator_choices(catalog, false);
        if (select_value(
                "Indicator binding", policy.indicator_id, indicators, [](const auto& item) { return item.id; },
                [](const auto& item) { return item.label; }))
            policy.output_id.clear();
        validation_at(errors, std::string(path) + "/indicatorId");
        const auto outputs = editor.compatible_output_choices(policy.indicator_id, catalog, false);
        if (std::ranges::none_of(outputs, [&](const auto& item) { return item.id == policy.output_id; }))
            policy.output_id = outputs.empty() ? std::string{} : outputs.front().id;
        select_value(
            "Output", policy.output_id, outputs, [](const auto& item) { return item.id; },
            [](const auto& item) { return item.label; });
        validation_at(errors, std::string(path) + "/outputId");
        ImGui::InputDouble("Offset", &policy.offset);
    } else {
        ImGui::InputDouble(policy.kind == Strategy::Core::PricePolicyKind::Absolute ? "Price" : "Percentage",
                           &policy.value);
        validation_at(errors, std::string(path) + "/value");
    }
    validation_at(errors, path);
    ImGui::PopID();
}

void condition_editor(const char* label, std::shared_ptr<Strategy::Core::ConditionExpression>& condition,
                      const Strategy::Core::Definition& definition,
                      std::span<const Analysis::Core::Indicator::Definition> catalog,
                      Didrachma::Studio::Core::StrategyEditor& editor, std::string_view path,
                      const std::vector<Strategy::Core::RepositoryError>& errors, int depth) {
    if (!condition)
        condition = std::make_shared<Strategy::Core::ConditionExpression>();
    ImGui::PushID(condition.get());
    if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen)) {
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
            editor.set_condition_kind(*condition, static_cast<Strategy::Core::ConditionKind>(kind));
        }
        if (condition->kind == Strategy::Core::ConditionKind::MarketComparison) {
            if (!std::holds_alternative<Strategy::Core::MarketComparison>(condition->predicate))
                condition->predicate = Strategy::Core::MarketComparison{};
            auto& value = std::get<Strategy::Core::MarketComparison>(condition->predicate);
            select_value(
                "Series", value.series_id, definition.series, [](const auto& item) { return item.id; },
                [&](const auto& item) { return editor.series_label(item.id); });
            validation_at(errors, std::string(path) + "/predicate/seriesId");
            const char* fields[]{"Open", "High", "Low", "Close", "Volume"};
            auto field = static_cast<int>(value.field);
            if (ImGui::Combo("Market field", &field, fields, 5))
                value.field = static_cast<Strategy::Core::MarketField>(field);
            const char* comparisons[]{"Less", "Less or equal", "Equal", "Greater or equal", "Greater"};
            auto comparison = static_cast<int>(value.comparison);
            if (ImGui::Combo("Comparison", &comparison, comparisons, 5))
                value.comparison = static_cast<Strategy::Core::Comparison>(comparison);
            ImGui::InputDouble("Value", &value.value);
            validation_at(errors, std::string(path) + "/predicate/value");
        } else if (condition->kind == Strategy::Core::ConditionKind::IndicatorComparison) {
            if (!std::holds_alternative<Strategy::Core::IndicatorComparison>(condition->predicate))
                condition->predicate = Strategy::Core::IndicatorComparison{};
            auto& value = std::get<Strategy::Core::IndicatorComparison>(condition->predicate);
            const auto choices = editor.compatible_indicator_choices(catalog, false);
            select_value(
                "Indicator", value.indicator_id, choices, [](const auto& item) { return item.id; },
                [](const auto& item) { return item.label; });
            validation_at(errors, std::string(path) + "/predicate/indicatorId");
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
            validation_at(errors, std::string(path) + "/predicate/outputId");
            const char* comparisons[]{"Less", "Less or equal", "Equal", "Greater or equal", "Greater"};
            auto comparison = static_cast<int>(value.comparison);
            if (ImGui::Combo("Comparison", &comparison, comparisons, 5))
                value.comparison = static_cast<Strategy::Core::Comparison>(comparison);
            ImGui::InputDouble("Value", &value.value);
            validation_at(errors, std::string(path) + "/predicate/value");
        } else if (condition->kind == Strategy::Core::ConditionKind::IndicatorCross) {
            if (!std::holds_alternative<Strategy::Core::IndicatorCross>(condition->predicate))
                condition->predicate = Strategy::Core::IndicatorCross{};
            auto& value = std::get<Strategy::Core::IndicatorCross>(condition->predicate);
            const auto choices = editor.compatible_indicator_choices(catalog, false);
            select_value(
                "Left indicator", value.left_indicator_id, choices, [](const auto& item) { return item.id; },
                [](const auto& item) { return item.label; });
            validation_at(errors, std::string(path) + "/predicate/leftIndicatorId");
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
            validation_at(errors, std::string(path) + "/predicate/leftOutputId");
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
                    "Right indicator", *value.right_indicator_id, choices, [](const auto& item) { return item.id; },
                    [](const auto& item) { return item.label; });
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
            validation_at(errors, std::string(path) + "/predicate/rightIndicatorId");
            validation_at(errors, std::string(path) + "/predicate/rightOutputId");
            validation_at(errors, std::string(path) + "/predicate");
            const char* directions[]{"Above", "Below", "Either"};
            auto direction = static_cast<int>(value.direction);
            if (ImGui::Combo("Cross direction", &direction, directions, 3))
                value.direction = static_cast<Strategy::Core::CrossDirection>(direction);
        } else if (condition->kind == Strategy::Core::ConditionKind::PatternOccurrence) {
            if (!std::holds_alternative<Strategy::Core::PatternOccurrence>(condition->predicate))
                condition->predicate = Strategy::Core::PatternOccurrence{};
            auto& value = std::get<Strategy::Core::PatternOccurrence>(condition->predicate);
            const auto choices = editor.compatible_indicator_choices(catalog, true);
            select_value(
                "Pattern", value.indicator_id, choices, [](const auto& item) { return item.id; },
                [](const auto& item) { return item.label; });
            validation_at(errors, std::string(path) + "/predicate/indicatorId");
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
            validation_at(errors, std::string(path) + "/predicate/durationSeconds");
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
            validation_at(errors, std::string(path) + "/predicate/percentage");
        } else if (condition->kind == Strategy::Core::ConditionKind::All ||
                   condition->kind == Strategy::Core::ConditionKind::Any ||
                   condition->kind == Strategy::Core::ConditionKind::Not ||
                   condition->kind == Strategy::Core::ConditionKind::Sequence) {
            for (std::size_t index = 0; index < condition->children.size();) {
                condition_editor(("Step " + std::to_string(index + 1)).c_str(), condition->children[index], definition,
                                 catalog, editor, std::string(path) + "/children/" + std::to_string(index), errors,
                                 depth + 1);
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
            validation_at(errors, std::string(path) + "/children");
            if (condition->kind == Strategy::Core::ConditionKind::Sequence) {
                bool time_limited = condition->sequence.maximum_elapsed.has_value();
                if (ImGui::Checkbox("Maximum elapsed", &time_limited))
                    condition->sequence.maximum_elapsed =
                        time_limited ? std::optional<Strategy::Core::Duration>{std::chrono::minutes{1}} : std::nullopt;
                if (time_limited) {
                    auto seconds = condition->sequence.maximum_elapsed->count();
                    if (ImGui::InputScalar("Elapsed seconds", ImGuiDataType_S64, &seconds))
                        condition->sequence.maximum_elapsed = Strategy::Core::Duration{seconds};
                    validation_at(errors, std::string(path) + "/sequence/maximumElapsedSeconds");
                }
                bool limited = condition->sequence.maximum_closed_bars.has_value();
                if (ImGui::Checkbox("Maximum closed bars", &limited))
                    condition->sequence.maximum_closed_bars = limited ? std::optional<std::uint64_t>{1} : std::nullopt;
                if (limited)
                    ImGui::InputScalar("Bar limit", ImGuiDataType_U64, &*condition->sequence.maximum_closed_bars);
                validation_at(errors, std::string(path) + "/sequence/maximumClosedBars");
            }
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}
} // namespace Didrachma::Apps::Studio::Intern
