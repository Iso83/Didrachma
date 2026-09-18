#pragma once

#include <Didrachma/studio/core/Events.h>

namespace Didrachma::Apps::Studio {
const Analysis::Core::Condition::Event* draw_event_panel(const ::Didrachma::Studio::Core::EventList& events,
                                                         const StockChart::Core::Document* active_chart, bool* open);
}
