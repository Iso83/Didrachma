#pragma once

#include "ChartView.h"

#include <imgui.h>

namespace Didrachma::Apps::Studio {
void draw_chart_axes(const ChartView& view, ImVec2 image_minimum, ImVec2 image_maximum);
bool draw_standalone_indicator_tabs(ChartView& view, StockChart::Core::Document& document, ImVec2 image_minimum);
} // namespace Didrachma::Apps::Studio
