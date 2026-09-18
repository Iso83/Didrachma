#pragma once

namespace Didrachma::StockChart::Render {
struct RenderCounters;
}

namespace Didrachma::Apps::Studio {
void draw_performance(const StockChart::Render::RenderCounters& counters);
}
