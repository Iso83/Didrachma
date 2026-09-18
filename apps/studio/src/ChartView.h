#pragma once

#include <Didrachma/analysis/adapters/talib/Analyzer.h>
#include <Didrachma/market/core/series/Bar.h>
#include <Didrachma/stockChart/core/Document.h>
#include <Didrachma/stockChart/render/CanvasHost.h>
#include <Didrachma/studio/core/Events.h>
#include <memory>
#include <vector>

namespace Didrachma::Apps::Studio {
std::string timeframe_label(Market::Core::Time::Frame timeframe);

struct ChartView {
    std::string id;
    std::unique_ptr<StockChart::Render::CanvasHost> canvas{std::make_unique<StockChart::Render::CanvasHost>()};
    StockChart::Render::Size size{640, 420};
    Market::Core::Time::Range visible_range;
    Market::Core::Time::Range history_range{};
    StockChart::Render::PriceRange price_range{};
    StockChart::Render::PriceRange volume_range{};
    StockChart::Render::PriceRange separate_range{};
    StockChart::Render::PaneLayout panes{};
    int plot_width{};
    bool open{true};
    bool has_visible_geometry{};
    bool middle_button_panning{};
    bool price_volume_resizing{};
    bool indicator_price_resizing{};
    std::vector<Market::Core::Series::Bar> bars;
};

void update_geometry(ChartView& view, StockChart::Core::Document& document,
                     Analysis::Adapters::TaLib::Analyzer& analyzer,
                     Didrachma::Studio::Core::EventList* events = nullptr);
} // namespace Didrachma::Apps::Studio
