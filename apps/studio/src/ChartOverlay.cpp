#include "ChartOverlay.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

namespace Didrachma::Apps::Studio {
void draw_chart_axes(const ChartView& view, ImVec2 image_minimum, ImVec2 image_maximum) {
    if (view.plot_width <= 0 || view.panes.price_content_height <= 0 || view.panes.volume_height <= 0 ||
        view.visible_range.empty())
        return;

    auto* draw_list = ImGui::GetWindowDrawList();
    const auto legend = [&](float y, ImU32 color, const char* label) {
        const ImVec2 minimum{image_minimum.x + 12.0F, image_minimum.y + y};
        draw_list->AddRectFilled(minimum, {minimum.x + 11.0F, minimum.y + 11.0F}, color);
        draw_list->AddText({minimum.x + 16.0F, minimum.y - 2.0F}, IM_COL32(215, 218, 226, 255), label);
    };
    legend(static_cast<float>(view.panes.price_top + 7), IM_COL32(115, 255, 0, 255), "Price | BB | Close / SMA");
    legend(static_cast<float>(view.panes.volume_top + 7), IM_COL32(255, 176, 56, 255), "Volume");

    const auto draw_value_axis = [&](StockChart::Render::PriceRange range, int top, int height, ImU32 color,
                                     const char* format) {
        for (int line = 0; line <= 4; ++line) {
            const auto value = range.maximum - line * (range.maximum - range.minimum) / 4.0;
            char label[24]{};
            std::snprintf(label, sizeof(label), format, value);
            const auto text_height = ImGui::CalcTextSize(label).y;
            const auto center = image_minimum.y + static_cast<float>(top + height * line / 4);
            const auto y = std::clamp(center - text_height * 0.5F, image_minimum.y + top,
                                      image_minimum.y + top + height - text_height);
            draw_list->AddText({image_minimum.x + static_cast<float>(view.plot_width) + 6.0F, y}, color, label);
        }
    };
    draw_value_axis(view.price_range, StockChart::Render::pane_content_top(view.panes.price_top),
                    StockChart::Render::pane_content_height(view.panes.price_content_height),
                    IM_COL32(185, 190, 205, 255), "%.2f");
    if (view.panes.separate_height > 0)
        draw_value_axis(view.separate_range, StockChart::Render::pane_content_top(view.panes.separate_top),
                        StockChart::Render::pane_content_height(view.panes.separate_height),
                        IM_COL32(180, 205, 225, 255), "%.2f");
    draw_value_axis(view.volume_range, StockChart::Render::pane_content_top(view.panes.volume_top),
                    StockChart::Render::pane_content_height(view.panes.volume_height), IM_COL32(205, 180, 130, 255),
                    "%.0f");

    for (int line = 0; line <= 5; ++line) {
        const auto duration = view.visible_range.end - view.visible_range.begin;
        const auto timestamp = view.visible_range.begin + duration * line / 5;
        const auto time = std::chrono::system_clock::to_time_t(timestamp);
        const auto utc = *std::gmtime(&time);
        char label[24]{};
        std::strftime(label, sizeof(label), "%d %b %H:%M", &utc);
        const auto label_width = ImGui::CalcTextSize(label).x;
        const auto plot_maximum = image_minimum.x + static_cast<float>(view.plot_width);
        const auto center = image_minimum.x + line * static_cast<float>(view.plot_width) / 5.0F;
        const auto x = StockChart::Render::axis_label_left(image_minimum.x, plot_maximum, center, label_width);
        draw_list->AddText({x, image_maximum.y + 4.0F}, IM_COL32(185, 190, 205, 255), label);
    }
}

bool draw_standalone_indicator_tabs(ChartView& view, StockChart::Core::Document& document, ImVec2 image_minimum) {
    const auto ids = document.standalone_indicator_ids();
    if (ids.empty() || view.panes.separate_height <= 0)
        return false;

    auto* draw_list = ImGui::GetWindowDrawList();
    const auto top = image_minimum.y + static_cast<float>(view.panes.separate_top) + 5.0F;
    auto left = image_minimum.x + 12.0F;
    draw_list->AddText({left, top}, IM_COL32(215, 218, 226, 255), "Indicators");
    left += ImGui::CalcTextSize("Indicators").x + 8.0F;
    constexpr float dropdown_width = 170.0F;
    const auto right = image_minimum.x + static_cast<float>(view.plot_width) - 8.0F;
    auto required_width = left;
    for (const auto& id : ids)
        if (const auto* entry = document.find_indicator(id))
            required_width += ImGui::CalcTextSize(entry->name.c_str()).x + 17.0F;
    const bool overflow = required_width > right;
    const auto dropdown_left = right - std::min(dropdown_width, std::max(70.0F, right - left));
    const auto tabs_right = overflow ? dropdown_left - 8.0F : right;
    bool changed = false;
    for (const auto& id : ids) {
        const auto* entry = document.find_indicator(id);
        if (!entry)
            continue;

        const auto text_size = ImGui::CalcTextSize(entry->name.c_str());
        if (left + text_size.x + 14.0F > tabs_right)
            break;

        draw_list->AddText({left, top}, IM_COL32(125, 132, 148, 255), "|");
        left += ImGui::CalcTextSize("|").x + 6.0F;
        const ImVec2 minimum{left - 3.0F, top - 2.0F};
        const ImVec2 maximum{left + text_size.x + 3.0F, top + text_size.y + 2.0F};
        const bool selected = document.selected_standalone_indicator_id() == id;
        if (selected)
            draw_list->AddRectFilled(minimum, maximum, IM_COL32(52, 91, 132, 230), 2.0F);
        draw_list->AddText({left, top}, selected ? IM_COL32(245, 247, 252, 255) : IM_COL32(180, 205, 225, 255),
                           entry->name.c_str());
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::IsMouseHoveringRect(minimum, maximum))
            changed |= document.select_standalone_indicator(id);
        left = maximum.x + 5.0F;
    }
    if (overflow) {
        const ImVec2 minimum{dropdown_left, top - 3.0F};
        const ImVec2 maximum{right, top + ImGui::GetTextLineHeight() + 3.0F};
        const auto* selected = document.selected_standalone_indicator_id()
                                   ? document.find_indicator(*document.selected_standalone_indicator_id())
                                   : nullptr;
        const std::string label = selected ? selected->name + "  v" : "Select indicator  v";
        draw_list->AddRectFilled(minimum, maximum, IM_COL32(38, 48, 64, 255), 2.0F);
        draw_list->AddRect(minimum, maximum, IM_COL32(95, 112, 140, 255), 2.0F);
        draw_list->PushClipRect(minimum, maximum, true);
        draw_list->AddText({minimum.x + 6.0F, top}, IM_COL32(225, 228, 235, 255), label.c_str());
        draw_list->PopClipRect();
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::IsMouseHoveringRect(minimum, maximum))
            ImGui::OpenPopup("##standalone-indicator-overflow");
        ImGui::SetNextWindowPos({minimum.x, maximum.y});
        if (ImGui::BeginPopup("##standalone-indicator-overflow")) {
            for (const auto& id : ids)
                if (const auto* entry = document.find_indicator(id))
                    if (ImGui::Selectable(entry->name.c_str(), document.selected_standalone_indicator_id() == id))
                        changed |= document.select_standalone_indicator(id);
            ImGui::EndPopup();
        }
    }
    return changed;
}
} // namespace Didrachma::Apps::Studio
