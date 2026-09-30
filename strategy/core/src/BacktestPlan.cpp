#include <Didrachma/strategy/core/Backtest.h>
#include <algorithm>
#include <map>

namespace Didrachma::Strategy::Core {
namespace Intern {
Duration planned_frame_duration(Market::Core::Time::Frame frame) {
    using enum Market::Core::Time::Unit;
    const auto unit = frame.unit == Minute ? std::chrono::minutes{1}
                      : frame.unit == Hour ? std::chrono::hours{1}
                                           : std::chrono::hours{24};
    return std::chrono::duration_cast<Duration>(unit) * frame.quantity;
}

std::string planned_key_text(const Market::Core::Series::Key& key) {
    return key.provider + '\n' + key.instrument + '\n' + std::to_string(key.timeframe.quantity) + '\n' +
           std::to_string(static_cast<int>(key.timeframe.unit));
}
} // namespace Intern

Market::Core::Time::UtcTimestamp exclusive_history_end(Market::Core::Time::UtcTimestamp inclusive_through) {
    return inclusive_through + std::chrono::seconds{1};
}

BacktestPlan plan_backtest(const BacktestRequest& request, Analysis::Core::Indicator::Analyzer& analyzer,
                           std::span<const BacktestSourceCapability> capabilities,
                           Market::Core::Time::UtcTimestamp now) {
    BacktestPlan plan;
    for (const auto& error : validate(request, analyzer.catalog()))
        plan.errors.push_back(
            {error.path == "/subjectSymbol" ? BacktestErrorCode::UnresolvedSubject : BacktestErrorCode::InvalidRequest,
             error.path, error.message});
    if (request.through > now)
        plan.errors.push_back({BacktestErrorCode::InvalidRequest, "/through", "Through must not be in the future"});

    DataGraph graph{Snapshot{request.strategy_snapshot}, analyzer, request.subject_symbol};
    plan.resolution_errors = graph.resolution().errors;
    for (const auto& error : plan.resolution_errors)
        plan.errors.push_back({BacktestErrorCode::UnresolvedSubject, error.binding_id, error.message});

    const auto capability_for = [&](Market::Core::Time::Frame frame) {
        return std::ranges::find(capabilities, frame, &BacktestSourceCapability::requested);
    };
    for (std::size_t index = 0; index < request.strategy_snapshot.series.size(); ++index) {
        const auto& target = request.strategy_snapshot.series[index];
        if (capability_for(target.timeframe) != capabilities.end())
            continue;

        const auto resolved_target =
            std::ranges::find(graph.resolution().series, target.id, &ResolvedSeries::binding_id);
        const SeriesBinding* selected{};
        Duration selected_duration{};
        if (resolved_target != graph.resolution().series.end())
            for (const auto& candidate : request.strategy_snapshot.series) {
                const auto resolved_candidate =
                    std::ranges::find(graph.resolution().series, candidate.id, &ResolvedSeries::binding_id);
                const auto candidate_duration = Intern::planned_frame_duration(candidate.timeframe);
                const auto target_duration = Intern::planned_frame_duration(target.timeframe);
                if (resolved_candidate == graph.resolution().series.end() || candidate.id == target.id ||
                    resolved_candidate->key.provider != resolved_target->key.provider ||
                    resolved_candidate->key.instrument != resolved_target->key.instrument ||
                    capability_for(candidate.timeframe) == capabilities.end() ||
                    candidate_duration >= target_duration || target_duration % candidate_duration != Duration::zero())
                    continue;

                if (!selected || candidate_duration > selected_duration) {
                    selected = &candidate;
                    selected_duration = candidate_duration;
                }
            }
        if (!selected) {
            plan.errors.push_back({BacktestErrorCode::UnsupportedTimeframe,
                                   "/strategy/series/" + std::to_string(index) + "/timeframe",
                                   "Provider does not support timeframe and no resolved source can derive it"});
        } else {
            graph.derive_series(target.id, selected->id);
            plan.derivations.emplace_back(target.id, selected->id);
        }
    }

    std::map<std::string, BacktestPlannedLoad> grouped;
    for (const auto& resolved : graph.resolution().series) {
        if (resolved.derived)
            continue;

        auto& load = grouped[Intern::planned_key_text(resolved.key)];
        load.request_key = resolved.key;
        load.binding_ids.push_back(resolved.binding_id);
        load.required_history =
            std::max(load.required_history, std::max<std::size_t>(graph.required_history(resolved.binding_id), 1));
        const auto capability = capability_for(resolved.key.timeframe);
        load.transport_source = capability == capabilities.end() ? resolved.key.timeframe : capability->source;
        load.history_reach = capability == capabilities.end() ? std::nullopt : capability->history_reach;
    }
    for (const auto& resolved : graph.resolution().series) {
        auto source = &resolved;
        while (source->derived && source->source_binding_id) {
            const auto found =
                std::ranges::find(graph.resolution().series, *source->source_binding_id, &ResolvedSeries::binding_id);
            if (found == graph.resolution().series.end())
                break;

            source = &*found;
        }
        if (auto group = grouped.find(Intern::planned_key_text(source->key)); group != grouped.end())
            group->second.dependent_binding_ids.push_back(resolved.binding_id);
    }
    for (auto& [key, load] : grouped) {
        (void)key;
        load.load_begin =
            request.from - Intern::planned_frame_duration(load.request_key.timeframe) *
                               static_cast<std::int64_t>(std::max<std::size_t>(load.required_history * 2, 4));
        load.exclusive_end = exclusive_history_end(request.through);
        if (load.history_reach && load.load_begin < now - *load.history_reach)
            plan.errors.push_back({BacktestErrorCode::InsufficientWarmup, load.binding_ids.front(),
                                   "Evaluation and warm-up begin before the source interval's " +
                                       std::to_string(load.history_reach->count()) + "-day history reach"});
        plan.loads.push_back(std::move(load));
    }

    for (std::size_t index = 0; index < request.strategy_snapshot.series.size(); ++index) {
        const auto& binding = request.strategy_snapshot.series[index];
        BacktestPlannedInput input;
        input.binding_id = binding.id;
        input.display_name = binding.display_name;
        input.requested = binding.timeframe;
        input.evaluation_from = request.from;
        input.evaluation_through = request.through;
        input.exclusive_end = exclusive_history_end(request.through);
        const auto resolved = std::ranges::find(graph.resolution().series, binding.id, &ResolvedSeries::binding_id);
        if (resolved != graph.resolution().series.end()) {
            input.resolved_key = resolved->key;
            input.derived = resolved->derived;
            auto source = &*resolved;
            while (source->derived && source->source_binding_id) {
                const auto found = std::ranges::find(graph.resolution().series, *source->source_binding_id,
                                                     &ResolvedSeries::binding_id);
                if (found == graph.resolution().series.end())
                    break;

                source = &*found;
            }
            const auto load =
                std::ranges::find(plan.loads, Intern::planned_key_text(source->key),
                                  [](const auto& item) { return Intern::planned_key_text(item.request_key); });
            if (load != plan.loads.end()) {
                input.provider_request_key = load->request_key;
                input.source = load->transport_source;
                input.derived = input.derived || input.source != input.requested;
                input.required_history = load->required_history;
                input.load_begin = load->load_begin;
                input.history_reach = load->history_reach;
            }
        }
        if (binding.provider_id != request.provider.id)
            input.errors.push_back({BacktestErrorCode::ProviderFailure,
                                    "/strategy/series/" + std::to_string(index) + "/providerId",
                                    "Series provider does not match the selected provider"});
        plan.errors.insert(plan.errors.end(), input.errors.begin(), input.errors.end());
        plan.inputs.push_back(std::move(input));
    }
    return plan;
}
} // namespace Didrachma::Strategy::Core
