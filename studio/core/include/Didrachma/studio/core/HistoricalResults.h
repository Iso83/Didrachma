#pragma once

#include <Didrachma/stockChart/core/Document.h>
#include <Didrachma/strategy/core/Backtest.h>
#include <Didrachma/studio/core/HistoricalBacktests.h>
#include <Didrachma/studio/core/Workspace.h>

namespace Didrachma::Studio::Core {
struct HistoricalResultDisplay {
    std::string subject;
    std::string inclusive_range;
    std::string terminal_status;
    std::string result_status;
    std::string entry_time, exit_time, entry_price, exit_price, exit_reason;
    std::string gross_profit_loss, net_profit_loss, return_percentage, duration, costs, ambiguity, warnings;
    std::string data_preparation, indicator_calculation, strategy_execution, total_runner;
};

enum class HistoricalTimelineKind { Evaluation, AuditEvent };

struct HistoricalTimelineItem {
    std::string id;
    HistoricalTimelineKind kind{HistoricalTimelineKind::Evaluation};
    Market::Core::Time::UtcTimestamp time;
    std::string result_kind;
    std::string condition_path;
    Strategy::Core::Truth truth{Strategy::Core::Truth::Unknown};
    std::optional<std::size_t> sequence_step;
    std::optional<std::size_t> sequence_size;
    std::string binding_id;
    std::optional<Market::Core::Series::Key> source_key;
    std::optional<Market::Core::Time::Frame> source_timeframe;
    std::optional<double> measured_value;
    std::string detail;
    std::optional<std::uint64_t> occurrence_id;

    [[nodiscard]] bool navigable() const {
        return source_key.has_value();
    }
};

struct HistoricalEvidenceGroup {
    std::optional<std::uint64_t> occurrence_id;
    std::optional<std::size_t> occurrence_number;
    std::string label;
    std::string entry_summary;
    std::string exit_summary;
    std::vector<HistoricalTimelineItem> items;
};

enum class StrategyOverlayAnnotationKind { Entry, Exit, Stop, Target };

struct StrategyOverlayAnnotation {
    StrategyOverlayAnnotationKind kind{};
    std::string label;
    Market::Core::Time::UtcTimestamp begin;
    Market::Core::Time::UtcTimestamp end;
    double price{};
    double price_minimum{};
    double price_maximum{};
};

struct StrategyOverlay {
    std::string run_id;
    std::uint64_t occurrence_id{};
    std::size_t occurrence_index{};
    Market::Core::Series::Key series;
    std::string strategy_name;
    Strategy::Core::Direction direction{Strategy::Core::Direction::Long};
    std::optional<Market::Core::Time::UtcTimestamp> entry_time, exit_time;
    std::optional<double> entry_price, exit_price;
    Strategy::Core::ExitReason exit_reason{Strategy::Core::ExitReason::None};
    double gross_profit_loss{};
    double net_profit_loss{};
    std::optional<Market::Core::Time::Range> position_span;
    bool profitable{};
    std::vector<StrategyOverlayAnnotation> annotations;
};

struct HistoricalChartSelection {
    StockChart::Core::Document* document{};
    std::span<const Market::Core::Series::Bar> retained_bars;
    Market::Core::Time::UtcTimestamp goto_time;
    bool created{};
    bool missing_history{};
    const StrategyOverlay* overlay{};
};

struct HistoricalOverlayAttachment {
    std::string chart_id;
    std::string run_id;
    std::uint64_t occurrence_id{};
    Market::Core::Series::Key series;
    bool enabled{true};
    bool selected{true};
};

struct HistoricalNavigation {
    Market::Core::Time::Range visible_range;
    bool applied{};
    bool missing_history{};
};

struct StrategyHoverValue {
    const StrategyOverlay* overlay{};
    std::optional<double> stop;
    std::optional<double> target;
};

[[nodiscard]] HistoricalResultDisplay present(const HistoricalBacktest&);
[[nodiscard]] std::vector<HistoricalTimelineItem> timeline(const HistoricalBacktest&);
[[nodiscard]] std::vector<HistoricalEvidenceGroup> evidence_groups(const HistoricalBacktest&);
[[nodiscard]] std::string format_utc(Market::Core::Time::UtcTimestamp);
[[nodiscard]] std::string format_timeframe(Market::Core::Time::Frame);
[[nodiscard]] std::string format_truth(Strategy::Core::Truth);
[[nodiscard]] std::string format_event_kind(Strategy::Core::StrategyEventKind);
[[nodiscard]] std::string format_exit_reason(Strategy::Core::ExitReason);

class HistoricalChartIntegration {
    struct PendingGoto {
        Market::Core::Time::UtcTimestamp target;
        std::string run_id;
    };

    std::vector<StrategyOverlay> m_overlays;
    std::vector<HistoricalOverlayAttachment> m_attached;
    std::map<std::string, PendingGoto> m_pending_gotos;

public:
    [[nodiscard]] HistoricalChartSelection select(Workspace&, const HistoricalBacktest&, const HistoricalTimelineItem&);
    [[nodiscard]] HistoricalNavigation navigate(StockChart::Core::Document&, Market::Core::Time::Range visible,
                                                Market::Core::Time::Range history,
                                                Market::Core::Time::UtcTimestamp target);
    [[nodiscard]] HistoricalNavigation history_loaded(StockChart::Core::Document&, Market::Core::Time::Range visible,
                                                      Market::Core::Time::Range history);
    [[nodiscard]] std::vector<const StrategyOverlay*> overlays_for(std::string_view chart_id) const;
    [[nodiscard]] std::span<const HistoricalOverlayAttachment> attachments() const {
        return m_attached;
    }
    [[nodiscard]] std::vector<StrategyHoverValue> hover_values(std::string_view chart_id,
                                                               Market::Core::Time::UtcTimestamp) const;
    bool set_enabled(std::string_view chart_id, std::string_view run_id, std::uint64_t occurrence_id,
                     const Market::Core::Series::Key&, bool);
    bool remove_run(std::string_view run_id);
    bool detach(std::string_view chart_id);
};

enum class HistoricalRemovalStatus { Removed, Active, NotFound };

struct HistoricalRemovalResult {
    HistoricalRemovalStatus status{HistoricalRemovalStatus::NotFound};
    std::size_t count{};
};

[[nodiscard]] HistoricalRemovalResult remove_historical_result(HistoricalBacktests&, HistoricalChartIntegration&,
                                                               std::string_view run_id, std::string& selected_run_id,
                                                               std::string& submitted_run_id);
[[nodiscard]] HistoricalRemovalResult clear_finished_historical_results(HistoricalBacktests&,
                                                                        HistoricalChartIntegration&,
                                                                        std::string& selected_run_id,
                                                                        std::string& submitted_run_id);
} // namespace Didrachma::Studio::Core
