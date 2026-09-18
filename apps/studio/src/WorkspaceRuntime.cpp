#include "WorkspaceRuntime.h"

#include <algorithm>

namespace Didrachma::Apps::Studio {
void initialize_demo_charts(Didrachma::Studio::Core::Workspace& workspace, std::vector<ChartView>& views,
                            Analysis::Adapters::TaLib::Analyzer& analyzer, Didrachma::Studio::Core::EventList& events,
                            std::span<const Market::Core::Series::Bar> demo_bars, Market::Core::Time::Range range) {
    workspace.create_chart({"demo", "TEST", {1, Market::Core::Time::Unit::Hour}}, range);
    workspace.create_chart({"demo", "SECOND", {1, Market::Core::Time::Unit::Hour}}, range);
    for (const auto& document : workspace.documents())
        views.push_back({document.id(), std::make_unique<StockChart::Render::CanvasHost>(), {640, 420}, range});
    for (auto& view : views) {
        view.canvas->resize(view.size);
        view.bars.assign(demo_bars.begin(), demo_bars.end());
        view.history_range = range;
        const auto document = std::ranges::find(workspace.documents(), view.id, &StockChart::Core::Document::id);
        update_geometry(view, *document, analyzer, &events);
    }
}

void open_yahoo_chart(Didrachma::Studio::Core::Workspace& workspace, std::vector<ChartView>& views,
                      YahooHistoryLoader& loader, const YahooChartRequest& request) {
    auto& document = workspace.create_chart(request.series, request.history, request.history);
    workspace.set_polling(document.id(), request.polling);
    views.push_back({document.id(), std::make_unique<StockChart::Render::CanvasHost>(), {640, 420}, request.history});
    views.back().history_range = request.history;
    views.back().canvas->resize(views.back().size);
    workspace.select_chart(document.id());
    loader.start(document.id(), request);
}

void restore_workspace_runtime(Didrachma::Studio::Core::Workspace& workspace, std::vector<ChartView>& views,
                               Analysis::Adapters::TaLib::Analyzer& analyzer,
                               Didrachma::Studio::Core::EventList& events, YahooHistoryLoader& loader,
                               std::span<const Market::Core::Series::Bar> demo_bars) {
    views.clear();
    events = {};
    for (auto& document : workspace.documents()) {
        const auto* request = workspace.history_request(document.id());
        if (!request)
            continue;

        views.push_back(
            {document.id(), std::make_unique<StockChart::Render::CanvasHost>(), {640, 420}, document.visible_range()});
        auto& view = views.back();
        view.history_range = request->range;
        view.canvas->resize(view.size);
        if (document.series().provider == "demo") {
            view.bars.assign(demo_bars.begin(), demo_bars.end());
            update_geometry(view, document, analyzer, &events);
        } else if (document.series().provider == "yahoo")
            loader.start(document.id(), {request->key, request->range, workspace.polling(document.id())});
    }
}
} // namespace Didrachma::Apps::Studio
