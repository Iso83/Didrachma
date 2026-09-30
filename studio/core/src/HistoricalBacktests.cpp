#include <Didrachma/studio/core/HistoricalBacktests.h>
#include <algorithm>

namespace Didrachma::Studio::Core {
namespace {
bool finished(const HistoricalBacktest& run) {
    if (!run.outcome)
        return false;

    const auto status = run.outcome->status;
    return status == Strategy::Core::BacktestStatus::Completed || status == Strategy::Core::BacktestStatus::Failed ||
           status == Strategy::Core::BacktestStatus::Cancelled;
}
} // namespace

HistoricalBacktests::HistoricalBacktests(HistoricalBacktestExecutor executor) : m_executor(std::move(executor)) {}

HistoricalBacktests::~HistoricalBacktests() {
    cancel();
}

std::string HistoricalBacktests::enqueue(const Strategy::Core::BacktestRequest& request) {
    if (busy())
        return {};

    const auto id = "backtest-" + std::to_string(m_next_id++);
    m_runs.push_back({id, request, {Strategy::Core::BacktestProgressKind::Preparing}, {}, std::nullopt});
    {
        std::scoped_lock lock{m_shared->mutex};
        m_shared->terminal_decision = Shared::TerminalDecision::Running;
    }
    const auto shared = m_shared;
    const auto executor = m_executor;
    m_worker = std::jthread([shared, executor, id, request] {
        const auto cancelled = [shared] {
            std::scoped_lock lock{shared->mutex};
            return shared->terminal_decision == Shared::TerminalDecision::CancellationWon;
        };
        const auto progress = [shared, id](const Strategy::Core::BacktestProgress& value) {
            std::scoped_lock lock{shared->mutex};
            if (value.kind != Strategy::Core::BacktestProgressKind::Completed &&
                value.kind != Strategy::Core::BacktestProgressKind::Failed &&
                value.kind != Strategy::Core::BacktestProgressKind::Cancelled)
                shared->messages.push_back({id, value, std::nullopt});
        };
        Strategy::Core::BacktestOutcome outcome;
        try {
            outcome = executor(request, cancelled, progress);
        } catch (const std::exception& error) {
            outcome.request = request;
            outcome.status = Strategy::Core::BacktestStatus::Failed;
            outcome.status_history.push_back(Strategy::Core::BacktestStatus::Failed);
            outcome.errors.push_back({Strategy::Core::BacktestErrorCode::EngineFailure, "/worker",
                                      "Background executor failed: " + std::string{error.what()}});
        } catch (...) {
            outcome.request = request;
            outcome.status = Strategy::Core::BacktestStatus::Failed;
            outcome.status_history.push_back(Strategy::Core::BacktestStatus::Failed);
            outcome.errors.push_back({Strategy::Core::BacktestErrorCode::EngineFailure, "/worker",
                                      "Background executor failed with an unknown exception"});
        }
        std::scoped_lock lock{shared->mutex};
        if (shared->terminal_decision == Shared::TerminalDecision::CancellationWon) {
            outcome.status = Strategy::Core::BacktestStatus::Cancelled;
            outcome.status_history = {Strategy::Core::BacktestStatus::Preparing,
                                      Strategy::Core::BacktestStatus::Cancelled};
            outcome.result_status = Strategy::Core::BacktestResultStatus::Failed;
            outcome.occurrences.clear();
            outcome.summary = {};
            outcome.errors = {{Strategy::Core::BacktestErrorCode::Cancelled, "/", "Backtest cancelled"}};
        } else
            shared->terminal_decision = Shared::TerminalDecision::OutcomeCommitted;
        shared->messages.push_back({id, std::nullopt, std::move(outcome)});
    });
    return id;
}

bool HistoricalBacktests::remove_finished(std::string_view id) {
    const auto run = std::ranges::find(m_runs, id, &HistoricalBacktest::id);
    if (run == m_runs.end() || !finished(*run))
        return false;

    m_runs.erase(run);
    return true;
}

std::vector<std::string> HistoricalBacktests::clear_finished() {
    std::vector<std::string> removed;
    for (const auto& run : m_runs)
        if (finished(run))
            removed.push_back(run.id);
    std::erase_if(m_runs, finished);
    return removed;
}

void HistoricalBacktests::cancel() {
    request_cancel();
    if (m_worker.joinable())
        m_worker.join();
    apply_pending();
}

void HistoricalBacktests::request_cancel() {
    std::scoped_lock lock{m_shared->mutex};
    if (m_shared->terminal_decision == Shared::TerminalDecision::Running)
        m_shared->terminal_decision = Shared::TerminalDecision::CancellationWon;
}

void HistoricalBacktests::apply_pending() {
    std::deque<Message> messages;
    {
        std::scoped_lock lock{m_shared->mutex};
        messages.swap(m_shared->messages);
    }
    for (auto& message : messages) {
        auto run = std::ranges::find(m_runs, message.id, &HistoricalBacktest::id);
        if (run == m_runs.end())
            continue;

        if (message.progress && !run->outcome) {
            run->progress = *message.progress;
            run->progress_history.push_back(*message.progress);
        }
        if (message.outcome && !run->outcome) {
            run->outcome = std::move(message.outcome);
            const auto status = run->outcome->status;
            run->progress.kind =
                status == Strategy::Core::BacktestStatus::Completed   ? Strategy::Core::BacktestProgressKind::Completed
                : status == Strategy::Core::BacktestStatus::Cancelled ? Strategy::Core::BacktestProgressKind::Cancelled
                                                                      : Strategy::Core::BacktestProgressKind::Failed;
            run->progress_history.push_back(run->progress);
        }
    }
    if (m_worker.joinable() && !m_runs.empty() && m_runs.back().outcome)
        m_worker.join();
}
} // namespace Didrachma::Studio::Core
