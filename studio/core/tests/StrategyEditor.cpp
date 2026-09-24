#include <Didrachma/studio/core/StrategyEditor.h>
#include <cassert>

using namespace Didrachma;

int main() {
    Strategy::Core::Definition stored;
    stored.id = "strategy";
    stored.display_name = "Original";
    Strategy::Core::ExecutionCosts stored_costs;
    stored_costs.quantity = 2;
    Studio::Core::StrategyEditor editor{stored, stored_costs};
    auto& primary = editor.add_series();
    primary.id = "primary";
    editor.draft().primary_series_id = primary.id;
    auto& peer = editor.add_series();
    peer.id = "peer";
    peer.instrument = {Strategy::Core::InstrumentKind::Fixed, "XLK"};
    assert(primary.id != peer.id);

    Analysis::Core::Indicator::Definition first;
    first.id = "SMA";
    first.parameters.push_back({"period", "Period", Analysis::Core::Indicator::ParameterKind::Integer, std::int64_t{20},
                                std::int64_t{2}, std::int64_t{200}});
    first.outputs.push_back({"value", "Value", Analysis::Core::Indicator::VisualKind::Line});
    Analysis::Core::Indicator::Definition second;
    second.id = "CDLTEST";
    second.capability = Analysis::Core::Indicator::Capability::AnalysisEvent;
    second.parameters.push_back(
        {"penetration", "Penetration", Analysis::Core::Indicator::ParameterKind::Real, 0.3, 0.0, 1.0});
    second.outputs.push_back({"pattern", "Pattern", Analysis::Core::Indicator::VisualKind::Marker});
    std::vector catalog{first, second};
    auto& indicator = editor.add_indicator(catalog, "SMA");
    indicator.series_id = "primary";
    assert(indicator.parameters.contains("period"));
    editor.select_indicator(indicator, second);
    assert(!indicator.parameters.contains("period") && indicator.parameters.contains("penetration"));
    assert(editor.compatible_indicators(catalog, true) == std::vector<std::string>{indicator.id});
    assert(editor.compatible_outputs(indicator.id, catalog, true) == std::vector<std::string>{"pattern"});

    for (int kind = static_cast<int>(Strategy::Core::ConditionKind::MarketComparison);
         kind <= static_cast<int>(Strategy::Core::ConditionKind::Sequence); ++kind) {
        const auto condition = editor.make_condition(static_cast<Strategy::Core::ConditionKind>(kind));
        assert(!condition->id.empty());
    }
    editor.draft().entry.condition = editor.make_condition(Strategy::Core::ConditionKind::IndicatorComparison);
    editor.draft().entry.condition->predicate =
        Strategy::Core::IndicatorComparison{indicator.id, "pattern", Strategy::Core::Comparison::Greater, 0};
    assert(editor.indicator_references(indicator.id));
    assert(!editor.remove_indicator(indicator.id));
    editor.draft().entry.condition = editor.make_condition(Strategy::Core::ConditionKind::All);
    auto& first_child = editor.add_child(*editor.draft().entry.condition);
    const auto first_child_id = first_child->id;
    editor.add_child(*editor.draft().entry.condition, Strategy::Core::ConditionKind::ClosedBarCount);
    assert(editor.move(editor.draft().entry.condition->children, 1, 0));
    assert(editor.draft().entry.condition->children[1]->id == first_child_id);
    assert(editor.remove_child(*editor.draft().entry.condition, 1));
    auto only = editor.make_condition(Strategy::Core::ConditionKind::Not);
    editor.add_child(*only);
    editor.add_child(*only);
    assert(only->children.size() == 1);
    assert(editor.remove_indicator(indicator.id));

    assert(!editor.remove_series("primary"));
    editor.draft().primary_series_id = "peer";
    assert(editor.remove_series("primary"));
    assert(editor.move(editor.draft().series, 0, 0) == false);

    auto& exit = editor.add_exit();
    const auto exit_id = exit.id;
    editor.add_exit();
    assert(editor.move(editor.draft().exits, 1, 0));
    assert(editor.draft().exits[1].id == exit_id);
    assert(editor.remove_exit(1));
    auto& rule = editor.add_rule();
    editor.add_action(rule);
    editor.add_action(rule);
    assert(editor.move(rule.actions, 1, 0));
    assert(editor.remove_action(rule, 1));
    assert(editor.remove_rule(0));

    editor.draft().display_name = "Changed";
    editor.draft_costs().quantity = 5;
    editor.changed();
    editor.cancel();
    assert(editor.draft().display_name == "Original" && editor.draft_costs().quantity == 2 && !editor.dirty());
    editor.draft().display_name = "Applied";
    editor.changed();
    editor.draft_costs().quantity = 3;
    editor.apply(stored, stored_costs);
    assert(stored.display_name == "Applied" && stored_costs.quantity == 3 && !editor.dirty());
}
