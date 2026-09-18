#include "YahooChartDialog.h"

#include <Didrachma/market/core/series/Bars.h>
#include <Didrachma/market/providers/yahoo/Interval.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <imgui.h>
#include <iterator>
#include <string>

namespace Didrachma::Apps::Studio {
namespace Intern {
std::optional<Market::Core::Time::UtcTimestamp> parse_date(const char* text) {
    int year{}, month{}, day{};
    char trailing{};
    if (std::sscanf(text, "%d-%d-%d%c", &year, &month, &day, &trailing) != 3)
        return {};

    const auto date = std::chrono::year{year} / std::chrono::month{static_cast<unsigned>(month)} /
                      std::chrono::day{static_cast<unsigned>(day)};
    if (!date.ok())
        return {};

    return std::chrono::sys_days{date};
}

void format_date(Market::Core::Time::UtcTimestamp timestamp, char* target, std::size_t size) {
    const auto time = std::chrono::system_clock::to_time_t(timestamp);
    const auto utc = *std::gmtime(&time);
    std::strftime(target, size, "%Y-%m-%d", &utc);
}

void set_suggested_range(Market::Core::Time::Frame frame, char* begin, std::size_t begin_size, char* end,
                         std::size_t end_size) {
    const auto now =
        std::chrono::time_point_cast<Market::Core::Time::UtcTimestamp::duration>(std::chrono::system_clock::now());
    const auto range = Market::Providers::Yahoo::suggested_history_range(frame, now);
    format_date(range.begin, begin, begin_size);
    format_date(range.end, end, end_size);
}
} // namespace Intern

void YahooHistoryLoader::start(std::string chart_id, YahooChartRequest request) {
    cancel(chart_id);
    m_pending.push_back(Pending{std::move(chart_id), request.polling, std::async(std::launch::async, [this, request] {
                                    return m_provider.load_history({request.series, request.history});
                                })});
}

void YahooHistoryLoader::cancel(const std::string& chart_id) {
    m_live.erase(chart_id);
    for (auto& pending : m_pending)
        if (pending.chart_id == chart_id)
            pending.chart_id.clear();
}

bool YahooHistoryLoader::apply_if_ready(Didrachma::Studio::Core::Workspace& workspace, std::vector<ChartView>& views,
                                        Analysis::Adapters::TaLib::Analyzer& analyzer,
                                        Didrachma::Studio::Core::EventList& events, std::string& status) {
    bool applied = false;
    const auto pending = std::ranges::find_if(m_pending, [](auto& item) {
        return item.result.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    });
    if (pending != m_pending.end()) {
        auto result = pending->result.get();
        const auto chart_id = pending->chart_id;
        const auto view = std::ranges::find(views, chart_id, &ChartView::id);
        const auto document = std::ranges::find(workspace.documents(), chart_id, &StockChart::Core::Document::id);
        if (auto* loaded = std::get_if<std::vector<Market::Core::Series::Bar>>(&result);
            !chart_id.empty() && view != views.end() && document != workspace.documents().end() && loaded) {
            view->bars = std::move(*loaded);
            update_geometry(*view, *document, analyzer, &events);
            if (pending->polling) {
                auto queue = std::make_unique<Market::Core::Provider::Queue>(256);
                auto* updates = queue.get();
                auto subscription = m_provider.subscribe(document->series(),
                                                         [updates](auto update) { updates->push(std::move(update)); });
                m_live[chart_id] = {std::move(queue), std::move(subscription)};
                status = m_live[chart_id].subscription ? "Yahoo Finance history loaded; polling for updates"
                                                       : "Yahoo Finance history loaded; polling unavailable";
            } else
                status = "Yahoo Finance history loaded (history mode)";
            applied = true;
        } else if (!chart_id.empty())
            if (const auto* error = std::get_if<Market::Core::Provider::Error>(&result))
                status = error->message;
        m_pending.erase(pending);
    }

    for (auto& [chart_id, runtime] : m_live) {
        auto updates = runtime.updates->drain();
        if (updates.empty())
            continue;
        const auto view = std::ranges::find(views, chart_id, &ChartView::id);
        const auto document = std::ranges::find(workspace.documents(), chart_id, &StockChart::Core::Document::id);
        if (view == views.end() || document == workspace.documents().end())
            continue;

        Market::Core::Series::Bars bars{document->series()};
        bars.apply({document->series(), Market::Core::Series::BarUpdateKind::Reset, view->bars});
        for (const auto& update : updates)
            bars.apply(update);
        view->bars.assign(bars.bars().begin(), bars.bars().end());
        update_geometry(*view, *document, analyzer, &events);
        status = "Yahoo Finance polling update applied";
        applied = true;
    }
    return applied;
}

std::optional<YahooChartRequest> draw_yahoo_chart_dialog(bool& open, std::string& status) {
    static char symbol[32] = "AAPL";
    static char history_begin[16]{};
    static char history_end[16]{};
    static int timeframe = 0;
    static bool polling = false;
    static bool initialized = false;

    std::optional<YahooChartRequest> request;
    if (!open)
        return request;

    const auto intervals = Market::Providers::Yahoo::intervals();
    if (!initialized) {
        Intern::set_suggested_range(intervals[static_cast<std::size_t>(timeframe)].frame, history_begin,
                                    std::size(history_begin), history_end, std::size(history_end));
        initialized = true;
    }

    ImGui::SetNextWindowSize({420, 0}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Yahoo Finance history", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Trade code", symbol, std::size(symbol));
        const auto label = intervals[static_cast<std::size_t>(timeframe)].display_name.data();
        if (ImGui::BeginCombo("Timeframe", label)) {
            for (std::size_t i = 0; i < intervals.size(); ++i) {
                const bool selected = timeframe == static_cast<int>(i);
                if (ImGui::Selectable(intervals[i].display_name.data(), selected)) {
                    timeframe = static_cast<int>(i);
                    Intern::set_suggested_range(intervals[i].frame, history_begin, std::size(history_begin),
                                                history_end, std::size(history_end));
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::InputText("History from (UTC)", history_begin, std::size(history_begin));
        ImGui::InputText("History to (UTC, exclusive)", history_end, std::size(history_end));
        ImGui::Checkbox("Poll for updates", &polling);
        ImGui::TextDisabled(polling ? "Mode: polling (30-second refresh)" : "Mode: history only");

        const auto& selected = intervals[static_cast<std::size_t>(timeframe)];
        if (selected.history_reach) {
            ImGui::TextWrapped("Yahoo source: %s; history is limited to approximately %lld days.",
                               selected.value.data(), static_cast<long long>(selected.history_reach->count()));
            if (selected.derived())
                ImGui::TextWrapped(
                    "10-minute bars are timestamp-resampled from Yahoo 5-minute history; the 5-minute limit applies.");
        } else
            ImGui::TextUnformatted("Yahoo does not impose an intraday reach limit for this interval.");

        if (ImGui::Button("Open chart")) {
            const auto begin = Intern::parse_date(history_begin);
            const auto end = Intern::parse_date(history_end);
            if (symbol[0] == '\0')
                status = "Enter a Yahoo Finance trade code";
            else if (!begin || !end || *begin >= *end)
                status = "Enter a valid UTC history range as YYYY-MM-DD";
            else {
                const auto now = std::chrono::time_point_cast<Market::Core::Time::UtcTimestamp::duration>(
                    std::chrono::system_clock::now());
                const auto validation = Market::Providers::Yahoo::validate_history(selected.frame, {*begin, *end}, now);
                if (!validation.valid) {
                    status = validation.message;
                    if (validation.earliest_allowed) {
                        const auto time = std::chrono::system_clock::to_time_t(*validation.earliest_allowed);
                        const auto utc = *std::gmtime(&time);
                        char date[16]{};
                        std::strftime(date, sizeof(date), "%Y-%m-%d", &utc);
                        status += "; earliest allowed date is " + std::string{date} + " UTC";
                    }
                } else {
                    request = YahooChartRequest{{"yahoo", symbol, intervals[static_cast<std::size_t>(timeframe)].frame},
                                                {*begin, *end},
                                                polling};
                    status = "Loading Yahoo Finance history...";
                    open = false;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            open = false;
    }
    ImGui::End();
    return request;
}
} // namespace Didrachma::Apps::Studio
