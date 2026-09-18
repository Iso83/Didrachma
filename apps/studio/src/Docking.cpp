#include "Docking.h"

#include <imgui_internal.h>

namespace Didrachma::Apps::Studio {
ImGuiID configure_default_docking(ImGuiID dockspace) {
    const auto* node = ImGui::DockBuilderGetNode(dockspace);
    if (node && (node->ChildNodes[0] || node->ChildNodes[1])) {
        const auto* central = ImGui::DockBuilderGetCentralNode(dockspace);
        return central ? central->ID : dockspace;
    }

    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);

    auto chart = dockspace;
    const auto left = ImGui::DockBuilderSplitNode(chart, ImGuiDir_Left, 0.18F, nullptr, &chart);
    const auto right = ImGui::DockBuilderSplitNode(chart, ImGuiDir_Right, 0.20F, nullptr, &chart);
    auto left_top = left;
    const auto left_bottom = ImGui::DockBuilderSplitNode(left_top, ImGuiDir_Down, 0.62F, nullptr, &left_top);
    auto right_top = right;
    const auto right_bottom = ImGui::DockBuilderSplitNode(right_top, ImGuiDir_Down, 0.55F, nullptr, &right_top);

    ImGui::DockBuilderDockWindow("Indicator Catalog", left_top);
    ImGui::DockBuilderDockWindow("Indicator Instances", left_bottom);
    ImGui::DockBuilderDockWindow("Profiles", right_top);
    ImGui::DockBuilderDockWindow("Analysis Events", right_bottom);
    ImGui::DockBuilderDockWindow("TEST - 1 Hour###chart-1", chart);
    ImGui::DockBuilderDockWindow("SECOND - 1 Hour###chart-2", chart);
    ImGui::DockBuilderFinish(dockspace);
    return chart;
}
} // namespace Didrachma::Apps::Studio
