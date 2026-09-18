#pragma once

#include "ChartView.h"
#include "YahooChartDialog.h"

#include <Didrachma/analysis/adapters/talib/Analyzer.h>
#include <Didrachma/market/core/series/Bar.h>
#include <Didrachma/studio/core/Events.h>
#include <Didrachma/studio/core/Workspace.h>
#include <span>

namespace Didrachma::Apps::Studio {
void draw_profile_panel(bool& open, Didrachma::Studio::Core::Workspace& workspace, std::vector<ChartView>& views,
                        Analysis::Adapters::TaLib::Analyzer& analyzer, Didrachma::Studio::Core::EventList& events,
                        YahooHistoryLoader& loader, std::span<const Market::Core::Series::Bar> demo_bars,
                        std::string& selected_instance, std::string& selected_profile, std::string& status,
                        Didrachma::Studio::Core::WorkspaceRepository& repository);
} // namespace Didrachma::Apps::Studio
