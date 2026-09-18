#include "Performance.h"

#include <Didrachma/stockChart/render/State.h>
#include <chrono>
#include <imgui.h>

namespace Didrachma::Apps::Studio {
void draw_performance(const StockChart::Render::RenderCounters& counters) {
    ImGui::TextDisabled("last %.2f ms | renders %llu | skipped %llu | rebuilds %llu | uploaded %llu | bars %llu/%llu",
                        std::chrono::duration<double, std::milli>(counters.last_render_time).count(),
                        static_cast<unsigned long long>(counters.canvas_renders),
                        static_cast<unsigned long long>(counters.skipped_frames),
                        static_cast<unsigned long long>(counters.layer_rebuilds),
                        static_cast<unsigned long long>(counters.uploaded_elements),
                        static_cast<unsigned long long>(counters.viewport_bars),
                        static_cast<unsigned long long>(counters.loaded_bars));
}
} // namespace Didrachma::Apps::Studio
