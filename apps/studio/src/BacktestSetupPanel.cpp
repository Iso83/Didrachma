#include "StrategyPanel.h"
#include "StrategyPanelWidgets.h"

#include <Didrachma/market/providers/yahoo/Interval.h>
#include <chrono>
#include <ctime>
#include <imgui.h>
#include <iomanip>
#include <sstream>

namespace Didrachma::Apps::Studio {
namespace {
std::vector<Strategy::Core::BacktestSourceCapability> yahoo_capabilities() {
    std::vector<Strategy::Core::BacktestSourceCapability> result;
    for (const auto& interval : Market::Providers::Yahoo::intervals())
        result.push_back({interval.frame, interval.source_frame, interval.history_reach});
    return result;
}

std::string frame_text(Market::Core::Time::Frame frame) {
    const char* unit = frame.unit == Market::Core::Time::Unit::Minute ? "minute"
                       : frame.unit == Market::Core::Time::Unit::Hour ? "hour"
                                                                      : "day";
    return std::to_string(frame.quantity) + " " + unit;
}

std::string utc_text(Market::Core::Time::UtcTimestamp value) {
    const auto time = std::chrono::system_clock::to_time_t(value);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream result;
    result << std::put_time(&utc, "%Y-%m-%d %H:%M:%S UTC");
    return result.str();
}

std::string input_fingerprint(const Didrachma::Studio::Core::BacktestSetupValues& values) {
    std::ostringstream result;
    result << values.subject << '\n'
           << values.from_date << '\n'
           << values.through_date << '\n'
           << values.provider_id << '\n'
           << values.execution.quantity << '\n'
           << values.execution.starting_capital.value_or(-1) << '\n'
           << values.execution.fixed_per_fill << '\n'
           << values.execution.percentage_per_fill << '\n'
           << values.execution.slippage_percentage << '\n'
           << values.fill_model_version;
    return result.str();
}
} // namespace

void draw_backtest_setup(Didrachma::Studio::Core::Strategies& strategies, StrategyPanelState& state) {
    if (!state.backtest_setup)
        return;

    bool open = true;
    ImGui::Begin("Backtest Run Setup", &open);
    auto& setup = *state.backtest_setup;
    auto& values = setup.values();
    ImGui::Text("Strategy: %s", setup.definition().display_name.c_str());
    ImGui::Text("Version: %u", setup.definition().version);
    const bool needs_subject = setup.requires_subject();
    if (needs_subject)
        Intern::text_input("Subject ticker", values.subject);
    Intern::text_input("From (UTC date)", values.from_date);
    ImGui::TextDisabled("Example: 2025-01-31 (starts 00:00:00 UTC)");
    Intern::text_input("Through (UTC date, inclusive)", values.through_date);
    ImGui::TextDisabled("Example: 2025-12-31 (includes through 23:59:59 UTC)");
    ImGui::Text("Provider: Yahoo Finance");
    ImGui::InputDouble("Quantity", &values.execution.quantity);
    bool has_capital = values.execution.starting_capital.has_value();
    if (ImGui::Checkbox("Starting capital", &has_capital))
        values.execution.starting_capital = has_capital ? std::optional{1.0} : std::nullopt;
    if (values.execution.starting_capital)
        ImGui::InputDouble("Capital amount", &*values.execution.starting_capital);
    ImGui::InputDouble("Fixed cost per fill", &values.execution.fixed_per_fill);
    ImGui::InputDouble("Percentage cost per fill", &values.execution.percentage_per_fill);
    ImGui::InputDouble("Slippage percentage", &values.execution.slippage_percentage);
    ImGui::Text("Fill model: Version 1");

    const auto fingerprint = input_fingerprint(values);
    if (!state.backtest_input_fingerprint.empty() && fingerprint != state.backtest_input_fingerprint)
        state.backtest_feedback.clear();
    state.backtest_input_fingerprint = fingerprint;

    const auto capabilities = yahoo_capabilities();
    const auto now = std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    const auto record = std::ranges::find(strategies.definitions(), setup.definition().id,
                                          [](const auto& item) { return item.definition.id; });
    const bool definition_uncommitted =
        record == strategies.definitions().end() || record->unsaved ||
        (state.editor && state.editor->dirty() && state.edited_definition == setup.definition().id);
    const auto checked = setup.validate(strategies.analyzer(), capabilities, now, definition_uncommitted);
    ImGui::SeparatorText("Resolved input preview");
    for (const auto& input : checked.preview.inputs) {
        ImGui::Text("%s — %s", input.display_name.c_str(), input.resolved_key.instrument.c_str());
        ImGui::BulletText("Yahoo Finance; requested %s; %s", frame_text(input.requested).c_str(),
                          input.derived ? ("derived from native " + frame_text(input.source)).c_str()
                                        : "provider-native");
        ImGui::BulletText("Provider request: %s; transport/source: %s",
                          frame_text(input.provider_request_key.timeframe).c_str(), frame_text(input.source).c_str());
        if (input.history_reach)
            ImGui::BulletText("Native source maximum reach: %lld days",
                              static_cast<long long>(input.history_reach->count()));
        else
            ImGui::BulletText("Native source maximum reach: provider full history");
        ImGui::BulletText("Evaluation: %s through %s UTC (inclusive)", values.from_date.c_str(),
                          values.through_date.c_str());
        ImGui::BulletText("Planned load/warm-up begin: %s (%zu source bars required)",
                          utc_text(input.load_begin).c_str(), input.required_history);
        if (input.resolved_key.instrument.empty())
            ImGui::TextColored({1, .4F, .4F, 1}, "Input unresolved");
        else if (input.errors.empty())
            ImGui::TextColored({.3F, .8F, .3F, 1}, "Input resolved");
        for (const auto& error : input.errors)
            ImGui::TextColored({1, .4F, .4F, 1}, "Invalid: %s", error.message.c_str());
    }
    for (const auto& error : checked.errors)
        ImGui::TextColored({1, .4F, .4F, 1}, "%s: %s", error.path.c_str(), error.message.c_str());
    if (checked.request)
        ImGui::TextColored({.3F, .8F, .3F, 1}, "Complete request is valid");
    else
        ImGui::TextColored({1, .4F, .4F, 1}, "Complete request is invalid");
    for (const auto& message : state.backtest_feedback)
        ImGui::TextColored({.8F, .8F, .3F, 1}, "%s", message.c_str());
    ImGui::BeginDisabled(!checked.request);
    if (ImGui::Button("Run backtest")) {
        if (setup.submit(strategies.analyzer(), capabilities, now, definition_uncommitted)) {
            Didrachma::Studio::Core::RecentBacktestSetupRepository repository{"didrachma-recent-backtest.json"};
            const auto errors = repository.save(values);
            const auto id =
                state.historical_backtests ? state.historical_backtests->enqueue(*setup.submission()) : std::string{};
            state.submitted_historical_run = id;
            state.backtest_feedback = {id.empty() ? "A historical backtest is already running"
                                                  : "Historical backtest enqueued as " + id};
            if (!errors.empty())
                state.backtest_feedback.push_back("Recent setup was not saved: " + errors.front().message);
        }
    }
    ImGui::EndDisabled();
    if (!state.submitted_historical_run.empty() && state.historical_backtests) {
        const auto run = std::ranges::find(state.historical_backtests->runs(), state.submitted_historical_run,
                                           &Didrachma::Studio::Core::HistoricalBacktest::id);
        if (run != state.historical_backtests->runs().end() && run->outcome) {
            ImGui::SameLine();
            if (ImGui::Button("View result"))
                state.selected_historical_run = run->id;
            ImGui::Text("Run %s: %s", run->id.c_str(), Didrachma::Studio::Core::present(*run).terminal_status.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        open = false;
    ImGui::End();
    if (!open)
        state.backtest_setup.reset();
}
} // namespace Didrachma::Apps::Studio
