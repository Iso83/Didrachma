#pragma once

#include "YahooChartDialog.h"

namespace Didrachma::Apps::Studio {
void initialize_demo_charts(Didrachma::Studio::Core::Workspace& workspace, std::vector<ChartView>& views,
                            Analysis::Adapters::TaLib::Analyzer& analyzer, Didrachma::Studio::Core::EventList& events,
                            std::span<const Market::Core::Series::Bar> demo_bars, Market::Core::Time::Range range);

void open_yahoo_chart(Didrachma::Studio::Core::Workspace& workspace, std::vector<ChartView>& views,
                      YahooHistoryLoader& loader, const YahooChartRequest& request);

void restore_workspace_runtime(Didrachma::Studio::Core::Workspace& workspace, std::vector<ChartView>& views,
                               Analysis::Adapters::TaLib::Analyzer& analyzer,
                               Didrachma::Studio::Core::EventList& events, YahooHistoryLoader& loader,
                               std::span<const Market::Core::Series::Bar> demo_bars);
} // namespace Didrachma::Apps::Studio
