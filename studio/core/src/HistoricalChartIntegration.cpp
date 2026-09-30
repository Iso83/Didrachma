#include <Didrachma/studio/core/HistoricalResults.h>
#include <algorithm>
#include <cmath>

namespace Didrachma::Studio::Core::Intern {
std::chrono::seconds duration(Market::Core::Time::Frame frame) {
    const auto unit = frame.unit == Market::Core::Time::Unit::Minute ? std::chrono::minutes{1}
                      : frame.unit == Market::Core::Time::Unit::Hour ? std::chrono::hours{1}
                                                                     : std::chrono::hours{24};
    return std::chrono::duration_cast<std::chrono::seconds>(unit) * frame.quantity;
}
} // namespace Didrachma::Studio::Core::Intern

namespace Didrachma::Studio::Core {
HistoricalChartSelection HistoricalChartIntegration::select(Workspace& workspace, const HistoricalBacktest& run,
                                                            const HistoricalTimelineItem& item) {
    if (!run.outcome || !item.source_key)
        return {};

    const auto input = std::ranges::find_if(run.outcome->inputs, [&](const auto& value) {
        return value.series.key == *item.source_key &&
               (!item.source_timeframe || value.series.key.timeframe == *item.source_timeframe);
    });
    if (input == run.outcome->inputs.end())
        return {};

    auto* document = workspace.find_chart(input->series.key);
    const bool created = !document;
    Market::Core::Time::Range retained{run.request.from, Strategy::Core::exclusive_history_end(run.request.through)};
    if (!input->bars.empty()) {
        retained.begin = input->bars.front().open_time;
        retained.end = input->bars.back().close_time.value_or(input->bars.back().open_time +
                                                              Intern::duration(input->series.key.timeframe));
    }

    if (!document)
        document = &workspace.create_chart(input->series.key, retained, retained);
    workspace.select_chart(document->id());

    const bool primary = input->series.binding_id == run.request.strategy_snapshot.primary_series_id;
    for (std::size_t index = 0; index < run.outcome->occurrences.size(); ++index) {
        const auto& occurrence = run.outcome->occurrences[index];
        auto overlay = std::ranges::find_if(m_overlays, [&](const auto& value) {
            return value.run_id == run.id && value.occurrence_id == occurrence.id && value.series == input->series.key;
        });
        if (overlay == m_overlays.end()) {
            const auto& result = occurrence.result;
            StrategyOverlay value;
            value.run_id = run.id;
            value.occurrence_id = occurrence.id;
            value.occurrence_index = index;
            value.series = input->series.key;
            value.strategy_name = run.request.strategy_snapshot.display_name;
            value.direction = run.request.strategy_snapshot.direction;
            value.entry_time = result.entry_time;
            value.exit_time = result.exit_time;
            value.entry_price = result.entry_price;
            value.exit_price = result.exit_price;
            value.exit_reason = result.exit_reason;
            value.gross_profit_loss = result.gross_profit_loss;
            value.net_profit_loss = result.net_profit_loss;
            value.profitable = result.net_profit_loss >= 0.0;
            if (primary && result.entry_time && result.exit_time)
                value.position_span = Market::Core::Time::Range{*result.entry_time, *result.exit_time};
            if (primary)
                for (const auto& marker : result.projection.markers) {
                    const auto entry = marker.kind == Strategy::Core::ProjectionKind::EntryMarker;
                    value.annotations.push_back(
                        {entry ? StrategyOverlayAnnotationKind::Entry : StrategyOverlayAnnotationKind::Exit,
                         entry ? "ENTRY" : "EXIT", marker.time, marker.time, marker.price, marker.price, marker.price});
                }
            if (primary)
                for (const auto& segment : result.projection.segments) {
                    if (segment.kind != Strategy::Core::ProjectionKind::StopPrice &&
                        segment.kind != Strategy::Core::ProjectionKind::TargetPrice)
                        continue;

                    const auto stop = segment.kind == Strategy::Core::ProjectionKind::StopPrice;
                    const auto width = std::max(std::abs(segment.price) * 0.001, 0.01);
                    value.annotations.push_back(
                        {stop ? StrategyOverlayAnnotationKind::Stop : StrategyOverlayAnnotationKind::Target,
                         stop ? "STOP" : "TARGET", segment.begin, segment.end, segment.price, segment.price - width,
                         segment.price + width});
                }
            m_overlays.push_back(std::move(value));
        }
        const auto attached = std::ranges::find_if(m_attached, [&](const auto& value) {
            return value.chart_id == document->id() && value.run_id == run.id && value.occurrence_id == occurrence.id &&
                   value.series == input->series.key;
        });
        if (attached == m_attached.end())
            m_attached.push_back({document->id(), run.id, occurrence.id, input->series.key, false, false});
    }
    auto selected_occurrence = item.occurrence_id;
    if (!selected_occurrence)
        for (const auto& occurrence : run.outcome->occurrences)
            if (occurrence.result.entry_time && occurrence.result.exit_time &&
                item.time >= *occurrence.result.entry_time && item.time <= *occurrence.result.exit_time) {
                selected_occurrence = occurrence.id;
                break;
            }

    if (!selected_occurrence && !run.outcome->occurrences.empty())
        selected_occurrence = run.outcome->occurrences.front().id;
    for (auto& value : m_attached)
        if (value.chart_id == document->id())
            value.selected = false;
    auto attached = std::ranges::find_if(m_attached, [&](const auto& value) {
        return value.chart_id == document->id() && value.run_id == run.id &&
               value.occurrence_id == selected_occurrence && value.series == input->series.key;
    });
    if (attached != m_attached.end()) {
        attached->enabled = true;
        attached->selected = true;
    }
    const auto* history = workspace.history_request(document->id());
    const bool missing = !history || item.time < history->range.begin || item.time >= history->range.end;
    if (missing) {
        const auto step = Intern::duration(input->series.key.timeframe);
        workspace.request_history(document->id(), {item.time - step * 100, item.time + step * 101});
        m_pending_gotos[document->id()] = {item.time, run.id};
    }
    const auto overlay = std::ranges::find_if(m_overlays, [&](const auto& value) {
        return value.run_id == run.id && value.occurrence_id == selected_occurrence &&
               value.series == input->series.key;
    });

    return {document, input->bars, item.time, created, missing, overlay == m_overlays.end() ? nullptr : &*overlay};
}

namespace {
Market::Core::Time::Range clamp_visible(Market::Core::Time::Range visible, Market::Core::Time::Range history) {
    auto duration = visible.end - visible.begin;
    const auto available = history.end - history.begin;
    if (duration > available)
        duration = available;
    if (visible.begin < history.begin)
        visible = {history.begin, history.begin + duration};
    if (visible.end > history.end)
        visible = {history.end - duration, history.end};
    return visible;
}
} // namespace

HistoricalNavigation HistoricalChartIntegration::navigate(StockChart::Core::Document& document,
                                                          Market::Core::Time::Range visible,
                                                          Market::Core::Time::Range history,
                                                          Market::Core::Time::UtcTimestamp target) {
    if (target < history.begin || target >= history.end) {
        const auto pending = m_pending_gotos.find(document.id());
        const auto run_id = pending == m_pending_gotos.end() ? std::string{} : pending->second.run_id;
        m_pending_gotos[document.id()] = {target, run_id};
        return {visible, false, true};
    }

    const auto half = (visible.end - visible.begin) / 2;
    const auto revealed = clamp_visible({target - half, target + half}, history);
    document.dispatch(StockChart::Core::SelectTimestamp{target});
    document.dispatch(StockChart::Core::NavigateViewport{revealed});
    m_pending_gotos.erase(document.id());
    return {revealed, true, false};
}

HistoricalNavigation HistoricalChartIntegration::history_loaded(StockChart::Core::Document& document,
                                                                Market::Core::Time::Range visible,
                                                                Market::Core::Time::Range history) {
    const auto pending = m_pending_gotos.find(document.id());
    return pending == m_pending_gotos.end() ? HistoricalNavigation{clamp_visible(visible, history), false, false}
                                            : navigate(document, visible, history, pending->second.target);
}

std::vector<const StrategyOverlay*> HistoricalChartIntegration::overlays_for(std::string_view chart_id) const {
    std::vector<const StrategyOverlay*> result;
    for (const auto& attached : m_attached) {
        if (attached.chart_id != chart_id || !attached.enabled)
            continue;

        const auto found = std::ranges::find_if(m_overlays, [&](const auto& value) {
            return value.run_id == attached.run_id && value.occurrence_id == attached.occurrence_id &&
                   value.series == attached.series;
        });
        if (found != m_overlays.end())
            result.push_back(&*found);
    }
    return result;
}

bool HistoricalChartIntegration::set_enabled(std::string_view chart_id, std::string_view run_id,
                                             std::uint64_t occurrence_id, const Market::Core::Series::Key& series,
                                             bool enabled) {
    const auto found = std::ranges::find_if(m_attached, [&](const auto& value) {
        return value.chart_id == chart_id && value.run_id == run_id && value.occurrence_id == occurrence_id &&
               value.series == series;
    });
    if (found == m_attached.end())
        return false;

    found->enabled = enabled;
    return true;
}

bool HistoricalChartIntegration::remove_run(std::string_view run_id) {
    const auto attachments = std::erase_if(m_attached, [&](const auto& value) { return value.run_id == run_id; });
    const auto overlays = std::erase_if(m_overlays, [&](const auto& value) { return value.run_id == run_id; });
    std::erase_if(m_pending_gotos, [&](const auto& value) { return value.second.run_id == run_id; });
    return attachments != 0 || overlays != 0;
}

std::vector<StrategyHoverValue> HistoricalChartIntegration::hover_values(std::string_view chart_id,
                                                                         Market::Core::Time::UtcTimestamp time) const {
    std::vector<StrategyHoverValue> result;
    for (const auto* overlay : overlays_for(chart_id)) {
        if (!overlay->position_span || time < overlay->position_span->begin || time > overlay->position_span->end)
            continue;

        StrategyHoverValue value{overlay};
        for (const auto& annotation : overlay->annotations) {
            if (time < annotation.begin || time > annotation.end)
                continue;

            if (annotation.kind == StrategyOverlayAnnotationKind::Stop)
                value.stop = annotation.price;
            else if (annotation.kind == StrategyOverlayAnnotationKind::Target)
                value.target = annotation.price;
        }
        result.push_back(value);
    }
    std::ranges::stable_sort(result, [](const auto& left, const auto& right) {
        return std::tie(left.overlay->run_id, left.overlay->occurrence_id) <
               std::tie(right.overlay->run_id, right.overlay->occurrence_id);
    });
    return result;
}

bool HistoricalChartIntegration::detach(std::string_view chart_id) {
    m_pending_gotos.erase(std::string{chart_id});
    return std::erase_if(m_attached, [&](const auto& value) { return value.chart_id == chart_id; }) != 0;
}

HistoricalRemovalResult remove_historical_result(HistoricalBacktests& backtests, HistoricalChartIntegration& charts,
                                                 std::string_view run_id, std::string& selected_run_id,
                                                 std::string& submitted_run_id) {
    const auto found = std::ranges::find(backtests.runs(), run_id, &HistoricalBacktest::id);
    if (found == backtests.runs().end())
        return {HistoricalRemovalStatus::NotFound, 0};

    if (!found->outcome || (found->outcome->status != Strategy::Core::BacktestStatus::Completed &&
                            found->outcome->status != Strategy::Core::BacktestStatus::Failed &&
                            found->outcome->status != Strategy::Core::BacktestStatus::Cancelled))
        return {HistoricalRemovalStatus::Active, 0};

    const std::string removed_id{run_id};
    if (!backtests.remove_finished(removed_id))
        return {HistoricalRemovalStatus::NotFound, 0};

    charts.remove_run(removed_id);
    if (selected_run_id == removed_id)
        selected_run_id = backtests.runs().empty() ? std::string{} : backtests.runs().back().id;
    if (submitted_run_id == removed_id)
        submitted_run_id.clear();

    return {HistoricalRemovalStatus::Removed, 1};
}

HistoricalRemovalResult clear_finished_historical_results(HistoricalBacktests& backtests,
                                                          HistoricalChartIntegration& charts,
                                                          std::string& selected_run_id, std::string& submitted_run_id) {
    const auto removed = backtests.clear_finished();
    for (const auto& id : removed)
        charts.remove_run(id);
    const auto still_exists = [&](const std::string& id) {
        return std::ranges::find(backtests.runs(), id, &HistoricalBacktest::id) != backtests.runs().end();
    };
    if (!selected_run_id.empty() && !still_exists(selected_run_id))
        selected_run_id = backtests.runs().empty() ? std::string{} : backtests.runs().back().id;
    if (!submitted_run_id.empty() && !still_exists(submitted_run_id))
        submitted_run_id.clear();

    return {HistoricalRemovalStatus::Removed, removed.size()};
}
} // namespace Didrachma::Studio::Core
