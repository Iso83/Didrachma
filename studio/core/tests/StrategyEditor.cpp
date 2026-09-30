#include <Didrachma/strategy/core/Repository.h>
#include <Didrachma/studio/core/StrategyEditor.h>
#include <cassert>
#include <filesystem>

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
    const auto primary_id = primary.id;
    auto& peer = editor.add_series();
    peer.id = "peer";
    peer.instrument = {Strategy::Core::InstrumentKind::Fixed, "XLK"};
    assert(primary_id != peer.id);

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

    // Every shared condition node is isolated on both sides of Apply/Cancel.
    stored.entry.condition = std::make_shared<Strategy::Core::ConditionExpression>();
    stored.entry.condition->id = "root-900";
    stored.entry.condition->kind = Strategy::Core::ConditionKind::All;
    auto nested = std::make_shared<Strategy::Core::ConditionExpression>();
    nested->id = "nested-42";
    nested->kind = Strategy::Core::ConditionKind::Not;
    nested->children.push_back(std::make_shared<Strategy::Core::ConditionExpression>());
    nested->children.front()->id = "leaf-77";
    stored.entry.condition->children.push_back(nested);
    stored.exits = {{"exit-81", nested}};
    stored.runtime_rules = {{"rule-61", 1, nested, {{Strategy::Core::ActionKind::Exit, {}, "original"}}}};
    Studio::Core::StrategyEditor isolated{stored};
    isolated.draft().entry.condition->children.front()->children.front()->id = "draft-leaf";
    isolated.draft().exits.front().condition->id = "draft-exit-condition";
    isolated.draft().runtime_rules.front().condition->id = "draft-rule-condition";
    isolated.draft().runtime_rules.front().actions.front().exit_reason = "draft";
    assert(stored.entry.condition->children.front()->children.front()->id == "leaf-77");
    assert(stored.exits.front().condition->id == "nested-42");
    assert(stored.runtime_rules.front().condition->id == "nested-42");
    assert(stored.runtime_rules.front().actions.front().exit_reason == "original");
    isolated.changed();
    isolated.cancel();
    assert(isolated.draft().entry.condition->children.front()->children.front()->id == "leaf-77");

    // A complete definition can be created and persisted using editor operations and metadata.
    Strategy::Core::Definition complete;
    complete.id = "editor-complete";
    Studio::Core::StrategyEditor builder{complete};
    builder.set_metadata("Complete editor strategy", "Built entirely through editor operations",
                         Strategy::Core::Direction::Long);
    auto& hourly = builder.add_series();
    const auto hourly_id = hourly.id;
    builder.name_series(hourly, "Subject 1 hour");
    builder.configure_series(hourly, "yahoo", {Strategy::Core::InstrumentKind::Subject, {}},
                             {1, Market::Core::Time::Unit::Hour});
    auto& daily = builder.add_series();
    const auto daily_id = daily.id;
    builder.name_series(daily, "Subject 1 day");
    builder.configure_series(daily, "yahoo", {Strategy::Core::InstrumentKind::Subject, {}},
                             {1, Market::Core::Time::Unit::Day});
    auto& fixed = builder.add_series();
    const auto fixed_id = fixed.id;
    builder.name_series(fixed, "Sector ETF");
    builder.configure_series(fixed, "yahoo", {Strategy::Core::InstrumentKind::Fixed, "XLK"},
                             {1, Market::Core::Time::Unit::Day});
    builder.select_primary(hourly_id);
    auto& study = builder.add_indicator(catalog, "SMA");
    builder.bind_indicator(study, hourly_id);
    builder.name_indicator(study, "Fast average");
    assert(builder.set_parameter(study, first.parameters.front(), std::int64_t{999}));
    assert(std::get<std::int64_t>(study.parameters.at("period")) == 200);
    const auto study_id = study.id;
    auto& pattern_binding = builder.add_indicator(catalog, "CDLTEST");
    builder.bind_indicator(pattern_binding, fixed_id);
    builder.name_indicator(pattern_binding, "Peer pattern");
    const auto pattern_id = pattern_binding.id;
    assert(builder.series_label(fixed_id) == "Sector ETF — XLK — 1 day");
    assert(builder.compatible_indicator_choices(catalog, true).front().label.find("Peer pattern") != std::string::npos);
    const auto price_indicators = builder.compatible_indicator_choices(catalog, false);
    assert(price_indicators.size() == 1 && price_indicators.front().id == study_id);
    assert(builder.compatible_output_choices(pattern_id, catalog, false).empty());
    assert(builder.compatible_output_choices(study_id, catalog, false).front().id == "value");

    auto root = builder.make_condition(Strategy::Core::ConditionKind::All);
    auto& any = builder.add_child(*root, Strategy::Core::ConditionKind::Any);
    auto& market = builder.add_child(*any, Strategy::Core::ConditionKind::MarketComparison);
    builder.set_predicate(*market, Strategy::Core::MarketComparison{hourly_id, Strategy::Core::MarketField::Close,
                                                                    Strategy::Core::Comparison::GreaterOrEqual, 90});
    auto& negated = builder.add_child(*any, Strategy::Core::ConditionKind::Not);
    auto& negated_leaf = negated->children.front();
    builder.set_predicate(*negated_leaf, Strategy::Core::MarketComparison{daily_id, Strategy::Core::MarketField::Close,
                                                                          Strategy::Core::Comparison::Less, 1});
    auto& sequence = builder.add_child(*root, Strategy::Core::ConditionKind::Sequence);
    builder.set_sequence_limits(*sequence, Strategy::Core::Duration{3600}, 4);
    auto& step = builder.add_child(*sequence, Strategy::Core::ConditionKind::PatternOccurrence);
    builder.set_predicate(*step, Strategy::Core::PatternOccurrence{pattern_id, {}});
    builder.set_entry_condition(root);
    builder.set_entry_order({Strategy::Core::EntryOrderKind::Limit, 100, 3});
    builder.set_stop({Strategy::Core::PricePolicyKind::PercentageFromEntry, 2});
    builder.set_target({Strategy::Core::PricePolicyKind::IndicatorValue, 0, study_id, "value", 1});
    auto& complete_exit = builder.add_exit();
    auto exit_condition = builder.make_condition(Strategy::Core::ConditionKind::ElapsedTime);
    builder.set_predicate(
        *exit_condition,
        Strategy::Core::ElapsedTimeCondition{Strategy::Core::Comparison::GreaterOrEqual, Strategy::Core::Duration{60}});
    builder.set_exit_condition(complete_exit, std::move(exit_condition));
    auto& complete_rule = builder.add_rule();
    auto rule_condition = builder.make_condition(Strategy::Core::ConditionKind::ClosedBarCount);
    builder.set_predicate(*rule_condition,
                          Strategy::Core::ClosedBarCountCondition{Strategy::Core::Comparison::GreaterOrEqual, 2});
    builder.set_rule(complete_rule, 7, std::move(rule_condition));
    auto& action = builder.add_action(complete_rule);
    builder.set_action(action, Strategy::Core::ActionKind::AdjustStop,
                       Strategy::Core::PricePolicy{Strategy::Core::PricePolicyKind::Absolute, 95});
    assert(builder.indicator_references(study_id));
    assert(!builder.remove_indicator(study_id));
    builder.set_target({Strategy::Core::PricePolicyKind::Absolute, 120});
    assert(builder.remove_indicator(study_id));
    auto& replacement = builder.add_indicator(catalog, "SMA");
    builder.bind_indicator(replacement, hourly_id);
    builder.name_indicator(replacement, "Fast average");
    assert(Strategy::Core::validate(builder.draft(), catalog).empty());

    Strategy::Core::Definition committed;
    builder.apply(committed);
    const auto path = std::filesystem::temp_directory_path() / "didrachma-strategy-editor.strategy.json";
    Strategy::Core::JsonFileRepository repository(path, catalog);
    assert(repository.save(committed).empty());
    const auto reloaded = repository.load();
    assert(std::holds_alternative<Strategy::Core::Definition>(reloaded));
    assert(Strategy::Core::serialize(std::get<Strategy::Core::Definition>(reloaded)) ==
           Strategy::Core::serialize(committed));
    std::filesystem::remove(path);

    // Dirty close/switch decisions never silently abandon or retarget a draft.
    Studio::Core::DraftTransitionGuard transitions;
    assert(!transitions.request({Studio::Core::DraftTransitionKind::SwitchDefinition, "other"}, true));
    assert(transitions.decide(Studio::Core::DraftDecision::ContinueEditing, true) ==
           Studio::Core::DraftDecisionResult::ContinueEditing);
    assert(!transitions.pending());
    assert(!transitions.request({Studio::Core::DraftTransitionKind::Close, {}}, true));
    assert(transitions.decide(Studio::Core::DraftDecision::Save, false) ==
           Studio::Core::DraftDecisionResult::SaveAsRequired);
    assert(transitions.pending());
    assert(transitions.decide(Studio::Core::DraftDecision::Discard, false) ==
           Studio::Core::DraftDecisionResult::Proceed);
    assert(transitions.complete_transition()->kind == Studio::Core::DraftTransitionKind::Close);
    assert(transitions.request({Studio::Core::DraftTransitionKind::SwitchDefinition, "clean"}, false));

    // The panel uses the guarded transition itself as the deferred mutation command. No definition or selection
    // changes until a successful save or an explicit discard completes that command.
    std::vector<std::string> records{"edited", "other"};
    std::string selected = "edited";
    const std::string dirty_bytes = Strategy::Core::serialize(builder.draft());
    Studio::Core::DraftTransitionGuard workflow;
    const auto execute = [&](const Studio::Core::DraftTransition& transition) {
        if (transition.kind == Studio::Core::DraftTransitionKind::Create) {
            records.push_back("created");
            selected = records.back();
        } else if (transition.kind == Studio::Core::DraftTransitionKind::SwitchDefinition)
            selected = transition.definition_id;
    };
    assert(Studio::Core::request_draft_transition(workflow,
                                                  {Studio::Core::DraftTransitionKind::SwitchDefinition, "edited"},
                                                  "edited", true) == Studio::Core::DraftRequestResult::SameDefinition);
    assert(!workflow.pending());
    assert(Strategy::Core::serialize(builder.draft()) == dirty_bytes);

    assert(Studio::Core::request_draft_transition(workflow, {Studio::Core::DraftTransitionKind::Create, {}}, "edited",
                                                  true) == Studio::Core::DraftRequestResult::DecisionRequired);
    assert(records.size() == 2 && selected == "edited");
    assert(workflow.decide(Studio::Core::DraftDecision::ContinueEditing, false) ==
           Studio::Core::DraftDecisionResult::ContinueEditing);
    assert(records.size() == 2 && selected == "edited" && Strategy::Core::serialize(builder.draft()) == dirty_bytes);

    assert(
        Studio::Core::request_draft_transition(workflow, {Studio::Core::DraftTransitionKind::SwitchDefinition, "other"},
                                               "edited", true) == Studio::Core::DraftRequestResult::DecisionRequired);
    assert(workflow.decide(Studio::Core::DraftDecision::Save, false) ==
           Studio::Core::DraftDecisionResult::SaveAsRequired);
    assert(workflow.pending() && selected == "edited");
    // A failed Save As leaves the same command and draft available for correction/retry.
    assert(workflow.pending()->definition_id == "other" && Strategy::Core::serialize(builder.draft()) == dirty_bytes);
    assert(workflow.decide(Studio::Core::DraftDecision::Save, true) ==
           Studio::Core::DraftDecisionResult::SaveThenProceed);
    execute(*workflow.complete_transition());
    assert(selected == "other" && records.size() == 2);

    assert(Studio::Core::request_draft_transition(workflow, {Studio::Core::DraftTransitionKind::Create, {}}, "edited",
                                                  true) == Studio::Core::DraftRequestResult::DecisionRequired);
    assert(workflow.decide(Studio::Core::DraftDecision::Discard, false) == Studio::Core::DraftDecisionResult::Proceed);
    execute(*workflow.complete_transition());
    assert(records.size() == 3 && selected == "created");
}
