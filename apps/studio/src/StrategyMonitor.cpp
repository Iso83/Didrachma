#include "StrategyPanel.h"

#include <algorithm>
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
} // namespace

void draw_strategy_monitor(
    Didrachma::Studio::Core::Strategies& strategies, StrategyPanelState& state,
    std::function<void(const Didrachma::Studio::Core::HistoricalBacktest&,
                       const Didrachma::Studio::Core::HistoricalTimelineItem&)>
        navigate_historical,
    std::function<void(std::string_view, std::optional<Market::Core::Time::UtcTimestamp>)> navigate) {
    ImGui::Begin("Strategy monitor");
    if (state.historical_backtests) {
        ImGui::SeparatorText("Historical backtest results");
        for (const auto& historical : state.historical_backtests->runs()) {
            const auto label = historical.id + " — " + historical.request.strategy_snapshot.display_name;
            if (ImGui::Selectable(label.c_str(), state.selected_historical_run == historical.id))
                state.selected_historical_run = historical.id;
        }
        const auto selected_before =
            std::ranges::find(state.historical_backtests->runs(), state.selected_historical_run,
                              &Didrachma::Studio::Core::HistoricalBacktest::id);
        ImGui::BeginDisabled(selected_before == state.historical_backtests->runs().end() || !selected_before->outcome);
        if (ImGui::Button("Delete selected result"))
            ImGui::OpenPopup("Confirm delete historical result");
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool has_finished = std::ranges::any_of(state.historical_backtests->runs(),
                                                      [](const auto& run) { return run.outcome.has_value(); });
        ImGui::BeginDisabled(!has_finished);
        if (ImGui::Button("Clear finished results"))
            ImGui::OpenPopup("Confirm clear historical results");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("Confirm delete historical result", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Delete the selected finished historical result and its strategy overlays?");
            if (ImGui::Button("Delete result")) {
                const auto removed = Didrachma::Studio::Core::remove_historical_result(
                    *state.historical_backtests, state.historical_charts, state.selected_historical_run,
                    state.selected_historical_run, state.submitted_historical_run);
                state.backtest_feedback = {removed.count == 1 ? "Historical result deleted"
                                                              : "Historical result was not deleted"};
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep result"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Confirm clear historical results", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped(
                "Delete all finished historical results and their strategy overlays? Active runs remain.");
            if (ImGui::Button("Clear finished")) {
                const auto removed = Didrachma::Studio::Core::clear_finished_historical_results(
                    *state.historical_backtests, state.historical_charts, state.selected_historical_run,
                    state.submitted_historical_run);
                state.backtest_feedback = {std::to_string(removed.count) + " historical result(s) deleted"};
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep results"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        const auto selected = std::ranges::find(state.historical_backtests->runs(), state.selected_historical_run,
                                                &Didrachma::Studio::Core::HistoricalBacktest::id);
        if (selected != state.historical_backtests->runs().end()) {
            const auto view = Didrachma::Studio::Core::present(*selected);
            ImGui::Text("Subject: %s", view.subject.c_str());
            ImGui::Text("Inclusive range: %s", view.inclusive_range.c_str());
            ImGui::Text("Terminal/result: %s / %s", view.terminal_status.c_str(), view.result_status.c_str());
            ImGui::Text("Aggregate gross/net P/L: %s / %s; costs %s", view.gross_profit_loss.c_str(),
                        view.net_profit_loss.c_str(), view.costs.c_str());
            ImGui::SeparatorText("Performance timings");
            ImGui::Text("Data preparation: %s", view.data_preparation.c_str());
            ImGui::Text("Indicator calculation: %s", view.indicator_calculation.c_str());
            ImGui::Text("Strategy execution: %s", view.strategy_execution.c_str());
            ImGui::Text("Total runner: %s", view.total_runner.c_str());
            ImGui::SeparatorText("Occurrences");
            if (selected->outcome)
                for (std::size_t index = 0; index < selected->outcome->occurrences.size(); ++index) {
                    const auto& occurrence = selected->outcome->occurrences[index];
                    if (!occurrence.result.entry_time)
                        continue;

                    const auto primary = std::ranges::find(selected->outcome->inputs,
                                                           selected->request.strategy_snapshot.primary_series_id,
                                                           [](const auto& input) { return input.series.binding_id; });
                    if (primary == selected->outcome->inputs.end())
                        continue;

                    const auto attachment =
                        std::ranges::find_if(state.historical_charts.attachments(), [&](const auto& value) {
                            return value.run_id == selected->id && value.occurrence_id == occurrence.id &&
                                   value.series == primary->series.key;
                        });
                    Didrachma::Studio::Core::HistoricalTimelineItem navigation_item;
                    navigation_item.time = *occurrence.result.entry_time;
                    navigation_item.source_key = primary->series.key;
                    navigation_item.source_timeframe = primary->series.key.timeframe;
                    navigation_item.occurrence_id = occurrence.id;
                    ImGui::PushID((selected->id + "-" + std::to_string(occurrence.id)).c_str());
                    const bool attached = attachment != state.historical_charts.attachments().end();
                    bool enabled = attached && attachment->enabled;
                    if (ImGui::Checkbox("##occurrence-overlay", &enabled)) {
                        if (!attached)
                            navigate_historical(*selected, navigation_item);
                        else
                            state.historical_charts.set_enabled(attachment->chart_id, attachment->run_id,
                                                                attachment->occurrence_id, attachment->series, enabled);
                    }
                    ImGui::SameLine();
                    const auto& result = occurrence.result;
                    const auto exit_time =
                        result.exit_time ? Didrachma::Studio::Core::format_utc(*result.exit_time) : "—";
                    const auto label = "#" + std::to_string(index + 1) + " (id " + std::to_string(occurrence.id) +
                                       ") " + Didrachma::Studio::Core::format_utc(*result.entry_time) + " — " +
                                       exit_time + " | " +
                                       Didrachma::Studio::Core::format_exit_reason(result.exit_reason) + " | net P/L " +
                                       std::to_string(result.net_profit_loss);
                    if (ImGui::Selectable(label.c_str(), attached && attachment->selected)) {
                        navigate_historical(*selected, navigation_item);
                    }
                    ImGui::PopID();
                }
            ImGui::SeparatorText("Chronological evidence");
            for (const auto& group : Didrachma::Studio::Core::evidence_groups(*selected)) {
                ImGui::PushID(group.label.c_str());
                if (ImGui::CollapsingHeader(group.label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (!group.entry_summary.empty())
                        ImGui::TextWrapped("%s", group.entry_summary.c_str());
                    if (!group.exit_summary.empty())
                        ImGui::TextWrapped("%s", group.exit_summary.c_str());
                    for (const auto& item : group.items) {
                        ImGui::PushID(item.id.c_str());
                        ImGui::Separator();
                        const auto type = item.result_kind == "Entry armed"  ? "Entry trigger / Entry armed"
                                          : item.result_kind == "Exit armed" ? "Exit trigger / Exit armed"
                                                                             : item.result_kind;
                        ImGui::TextWrapped("%s — %s", Didrachma::Studio::Core::format_utc(item.time).c_str(),
                                           type.c_str());
                        ImGui::TextWrapped("Condition: %s",
                                           item.condition_path.empty() ? "—" : item.condition_path.c_str());
                        ImGui::TextWrapped("Truth: %s    Measured value: %s",
                                           Didrachma::Studio::Core::format_truth(item.truth).c_str(),
                                           item.measured_value ? std::to_string(*item.measured_value).c_str() : "—");
                        ImGui::TextWrapped("Detail: %s", item.detail.empty() ? "—" : item.detail.c_str());
                        const auto source = item.source_key
                                                ? item.source_key->provider + "/" + item.source_key->instrument
                                                : std::string{"—"};
                        const auto timeframe = item.source_timeframe
                                                   ? Didrachma::Studio::Core::format_timeframe(*item.source_timeframe)
                                                   : std::string{"—"};
                        ImGui::TextWrapped("Binding: %s    Source: %s    Timeframe: %s",
                                           item.binding_id.empty() ? "—" : item.binding_id.c_str(), source.c_str(),
                                           timeframe.c_str());
                        ImGui::TextWrapped("Source bar: %s",
                                           item.source_key ? Didrachma::Studio::Core::format_utc(item.time).c_str()
                                                           : "—");
                        if (item.sequence_step && item.sequence_size)
                            ImGui::Text("Sequence progress: %zu/%zu", *item.sequence_step, *item.sequence_size);
                        if (item.navigable() && ImGui::Button("Go to chart"))
                            navigate_historical(*selected, item);
                        ImGui::PopID();
                    }
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::SeparatorText("Active polling/live runs");
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
