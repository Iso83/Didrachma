#pragma once

#include <Didrachma/strategy/core/Backtest.h>
#include <deque>
#include <mutex>
#include <thread>

namespace Didrachma::Studio::Core {
struct HistoricalBacktest {
    std::string id;
    Strategy::Core::BacktestRequest request;
    Strategy::Core::BacktestProgress progress;
    std::vector<Strategy::Core::BacktestProgress> progress_history;
    std::optional<Strategy::Core::BacktestOutcome> outcome;
};

using HistoricalBacktestExecutor =
    std::function<Strategy::Core::BacktestOutcome(const Strategy::Core::BacktestRequest&, const std::function<bool()>&,
                                                  const Strategy::Core::BacktestProgressObserver&)>;

class HistoricalBacktests {
    struct Message {
        std::string id;
        std::optional<Strategy::Core::BacktestProgress> progress;
        std::optional<Strategy::Core::BacktestOutcome> outcome;
    };
    struct Shared {
        enum class TerminalDecision { Running, CancellationWon, OutcomeCommitted };

        std::mutex mutex;
        std::deque<Message> messages;
        TerminalDecision terminal_decision{TerminalDecision::Running};
    };

    HistoricalBacktestExecutor m_executor;
    std::shared_ptr<Shared> m_shared{std::make_shared<Shared>()};
    std::vector<HistoricalBacktest> m_runs;
    std::jthread m_worker;
    std::uint64_t m_next_id{1};

public:
    explicit HistoricalBacktests(HistoricalBacktestExecutor);
    ~HistoricalBacktests();
    HistoricalBacktests(const HistoricalBacktests&) = delete;
    HistoricalBacktests& operator=(const HistoricalBacktests&) = delete;

    [[nodiscard]] std::span<const HistoricalBacktest> runs() const {
        return m_runs;
    }
    [[nodiscard]] bool busy() const {
        return m_worker.joinable();
    }

    [[nodiscard]] std::string enqueue(const Strategy::Core::BacktestRequest&);
    [[nodiscard]] bool remove_finished(std::string_view id);
    [[nodiscard]] std::vector<std::string> clear_finished();
    void request_cancel();
    void cancel();
    void apply_pending();
};
} // namespace Didrachma::Studio::Core
