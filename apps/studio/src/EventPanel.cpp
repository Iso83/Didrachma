#include "EventPanel.h"

#include <ctime>
#include <imgui.h>

namespace Didrachma::Apps::Studio {
namespace Intern {
const char* direction(Analysis::Core::Condition::Direction value) {
    using enum Analysis::Core::Condition::Direction;
    if (value == Upward)
        return "Upward";

    if (value == Downward)
        return "Downward";

    return "Neutral";
}
} // namespace Intern

const Analysis::Core::Condition::Event* draw_event_panel(const ::Didrachma::Studio::Core::EventList& events,
                                                         const StockChart::Core::Document* active_chart,
                                                         std::set<std::string>& highlighted_event_ids, bool* open) {
    const Analysis::Core::Condition::Event* selected{};
    ImGui::Begin("Analysis Events", open);
    if (!active_chart)
        ImGui::TextDisabled("Select a StockChart");
    else {
        const ::Didrachma::Studio::Core::EventFilter filter{active_chart->series().instrument, std::nullopt,
                                                            active_chart->id()};

        for (const auto* event : events.filtered(filter)) {
            const auto value = std::chrono::system_clock::to_time_t(event->start);
            const auto utc = *std::gmtime(&value);
            char timestamp[32]{};
            std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M UTC", &utc);
            ImGui::PushID(event->id.c_str());
            bool highlighted = highlighted_event_ids.contains(event->id);
            if (ImGui::Checkbox("##highlight", &highlighted)) {
                if (highlighted)
                    highlighted_event_ids.insert(event->id);
                else
                    highlighted_event_ids.erase(event->id);
            }
            ImGui::SameLine();
            if (ImGui::Selectable(event->title.c_str(), active_chart->selected_event_id() == event->id)) {
                highlighted_event_ids.insert(event->id);
                selected = event;
            }

            ImGui::TextDisabled("%s | %s | Attention", timestamp, Intern::direction(event->direction));
            ImGui::TextWrapped("%s", event->summary.c_str());
            if (!event->source_name.empty()) {
                ImGui::Text("Indicator: %s", event->source_name.c_str());
                for (const auto& [name, value] : event->source_parameters)
                    ImGui::TextDisabled("%s: %s", name.c_str(), value.c_str());
            }
            if (ImGui::TreeNode("Evidence")) {
                for (const auto& [name, number] : event->evidence)
                    ImGui::Text("%s: %.4f", name.c_str(), number);
                ImGui::TreePop();
            }
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    ImGui::End();
    return selected;
}
} // namespace Didrachma::Apps::Studio
