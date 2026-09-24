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
