#pragma once

#include "ChartView.h"

#include <Didrachma/market/core/provider/Queue.h>
#include <Didrachma/market/core/series/Key.h>
#include <Didrachma/market/core/time/Range.h>
#include <Didrachma/market/providers/yahoo/Provider.h>
#include <Didrachma/studio/core/Events.h>
#include <Didrachma/studio/core/Workspace.h>
#include <future>
#include <map>
#include <optional>
#include <string>

namespace Didrachma::Apps::Studio {
struct YahooChartRequest {
    Market::Core::Series::Key series;
    Market::Core::Time::Range history;
    bool polling{};
};

class YahooHistoryLoader {
    struct Pending {
        std::string chart_id;
        bool polling{};
        std::future<Market::Core::Provider::HistoryResult> result;
    };

    Market::Providers::Yahoo::Provider m_provider;
    std::vector<Pending> m_pending;
    struct LiveRuntime {
        std::unique_ptr<Market::Core::Provider::Queue> updates;
        std::unique_ptr<Market::Core::Provider::Subscription> subscription;
    };
    std::map<std::string, LiveRuntime> m_live;

public:
    [[nodiscard]] bool busy() const {
        return !m_pending.empty();
    }

    void start(std::string chart_id, YahooChartRequest request);
    void cancel(const std::string& chart_id);
    bool apply_if_ready(Didrachma::Studio::Core::Workspace& workspace, std::vector<ChartView>& views,
                        Analysis::Adapters::TaLib::Analyzer& analyzer, Didrachma::Studio::Core::EventList& events,
                        std::string& status);
};

std::optional<YahooChartRequest> draw_yahoo_chart_dialog(bool& open, std::string& status);
} // namespace Didrachma::Apps::Studio
