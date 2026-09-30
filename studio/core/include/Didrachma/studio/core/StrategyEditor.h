#pragma once

#include <Didrachma/analysis/core/indicator/Definition.h>
#include <Didrachma/strategy/core/Model.h>
#include <Didrachma/strategy/core/Runtime.h>
#include <span>

namespace Didrachma::Studio::Core {
struct RemovalBlock {
    std::vector<std::string> references;

    [[nodiscard]] explicit operator bool() const {
        return !references.empty();
    }
};

struct LabeledId {
    std::string id;
    std::string label;
};

enum class DraftTransitionKind { Close, SwitchDefinition, Create, Duplicate, Import, Delete };
enum class DraftDecision { Save, Discard, ContinueEditing };
enum class DraftDecisionResult { ContinueEditing, Proceed, SaveThenProceed, SaveAsRequired };
enum class DraftRequestResult { SameDefinition, Proceed, DecisionRequired };

struct DraftTransition {
    DraftTransitionKind kind{DraftTransitionKind::Close};
    std::string definition_id;
};

class DraftTransitionGuard {
    std::optional<DraftTransition> m_pending;

public:
    [[nodiscard]] const std::optional<DraftTransition>& pending() const {
        return m_pending;
    }

    bool request(DraftTransition transition, bool dirty);
    DraftDecisionResult decide(DraftDecision, bool has_existing_path);
    std::optional<DraftTransition> complete_transition();
};

DraftRequestResult request_draft_transition(DraftTransitionGuard&, DraftTransition, std::string_view edited_definition,
                                            bool dirty);

class StrategyEditor {
    Strategy::Core::Definition m_original;
    Strategy::Core::Definition m_draft;
    Strategy::Core::ExecutionCosts m_original_costs;
    Strategy::Core::ExecutionCosts m_draft_costs;
    bool m_dirty{};
    std::uint64_t m_next_id{1};

    [[nodiscard]] std::string unique_id(std::string_view prefix);

public:
    explicit StrategyEditor(const Strategy::Core::Definition&, Strategy::Core::ExecutionCosts costs = {});

    [[nodiscard]] const Strategy::Core::Definition& original() const {
        return m_original;
    }
    [[nodiscard]] Strategy::Core::Definition& draft() {
        return m_draft;
    }
    [[nodiscard]] const Strategy::Core::Definition& draft() const {
        return m_draft;
    }
    [[nodiscard]] bool dirty() const {
        return m_dirty;
    }
    [[nodiscard]] Strategy::Core::ExecutionCosts& draft_costs() {
        return m_draft_costs;
    }

    void changed() {
        m_dirty = true;
    }
    void cancel();
    void apply(Strategy::Core::Definition& destination);
    void apply(Strategy::Core::Definition& destination, Strategy::Core::ExecutionCosts& costs);

    Strategy::Core::SeriesBinding& add_series();
    Strategy::Core::IndicatorBinding& add_indicator(std::span<const Analysis::Core::Indicator::Definition> catalog,
                                                    std::string_view definition_id = {});
    Strategy::Core::ExitCondition& add_exit();
    Strategy::Core::RuntimeRule& add_rule();
    Strategy::Core::RuntimeAction& add_action(Strategy::Core::RuntimeRule&);
    std::shared_ptr<Strategy::Core::ConditionExpression> make_condition(Strategy::Core::ConditionKind);
    std::shared_ptr<Strategy::Core::ConditionExpression>&
    add_child(Strategy::Core::ConditionExpression&,
              Strategy::Core::ConditionKind kind = Strategy::Core::ConditionKind::MarketComparison);
    void select_indicator(Strategy::Core::IndicatorBinding&, const Analysis::Core::Indicator::Definition&);
    bool set_condition_kind(Strategy::Core::ConditionExpression&, Strategy::Core::ConditionKind);
    bool set_parameter(Strategy::Core::IndicatorBinding&, const Analysis::Core::Indicator::ParameterDefinition&,
                       Analysis::Core::Indicator::ParameterValue);
    void name_series(Strategy::Core::SeriesBinding&, std::string);
    void configure_series(Strategy::Core::SeriesBinding&, std::string provider, Strategy::Core::InstrumentSelector,
                          Market::Core::Time::Frame);
    void select_primary(std::string);
    void name_indicator(Strategy::Core::IndicatorBinding&, std::string);
    void set_metadata(std::string display_name, std::string description, Strategy::Core::Direction);
    void bind_indicator(Strategy::Core::IndicatorBinding&, std::string series_id);
    void set_entry_condition(std::shared_ptr<Strategy::Core::ConditionExpression>);
    void set_predicate(Strategy::Core::ConditionExpression&, Strategy::Core::ConditionPredicate);
    void set_sequence_limits(Strategy::Core::ConditionExpression&, std::optional<Strategy::Core::Duration>,
                             std::optional<std::uint64_t>);
    void set_entry_order(Strategy::Core::EntryOrder);
    void set_stop(Strategy::Core::PricePolicy);
    void set_target(Strategy::Core::PricePolicy);
    void set_exit_condition(Strategy::Core::ExitCondition&, std::shared_ptr<Strategy::Core::ConditionExpression>);
    void set_rule(Strategy::Core::RuntimeRule&, int priority, std::shared_ptr<Strategy::Core::ConditionExpression>);
    void set_action(Strategy::Core::RuntimeAction&, Strategy::Core::ActionKind,
                    std::optional<Strategy::Core::PricePolicy>, std::string reason = {});

    [[nodiscard]] std::string series_label(std::string_view id) const;
    [[nodiscard]] std::string indicator_label(std::string_view id) const;
    [[nodiscard]] std::vector<LabeledId> series_choices() const;
    [[nodiscard]] std::vector<LabeledId>
    compatible_indicator_choices(std::span<const Analysis::Core::Indicator::Definition>, bool patterns) const;
    [[nodiscard]] std::vector<LabeledId>
    compatible_output_choices(std::string_view indicator_id, std::span<const Analysis::Core::Indicator::Definition>,
                              bool markers = false) const;

    [[nodiscard]] std::vector<std::string>
    compatible_indicators(std::span<const Analysis::Core::Indicator::Definition> catalog, bool patterns) const;
    [[nodiscard]] std::vector<std::string>
    compatible_outputs(std::string_view indicator_id, std::span<const Analysis::Core::Indicator::Definition> catalog,
                       bool markers = false) const;

    [[nodiscard]] RemovalBlock series_references(std::string_view id) const;
    [[nodiscard]] RemovalBlock indicator_references(std::string_view id) const;
    bool remove_series(std::string_view id);
    bool remove_indicator(std::string_view id);
    bool remove_child(Strategy::Core::ConditionExpression&, std::size_t);
    bool remove_exit(std::size_t);
    bool remove_rule(std::size_t);
    bool remove_action(Strategy::Core::RuntimeRule&, std::size_t);

    template <class T> bool move(std::vector<T>& values, std::size_t from, std::size_t to) {
        if (from >= values.size() || to >= values.size() || from == to)
            return false;

        auto value = std::move(values[from]);
        values.erase(values.begin() + static_cast<std::ptrdiff_t>(from));
        values.insert(values.begin() + static_cast<std::ptrdiff_t>(to), std::move(value));
        m_dirty = true;
        return true;
    }
};
} // namespace Didrachma::Studio::Core
