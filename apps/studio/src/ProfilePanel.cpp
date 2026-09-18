#include "ProfilePanel.h"

#include "IndicatorActions.h"
#include "WorkspaceRuntime.h"

#include <algorithm>
#include <imgui.h>

namespace Didrachma::Apps::Studio {
void draw_profile_panel(bool& open, Didrachma::Studio::Core::Workspace& workspace, std::vector<ChartView>& views,
                        Analysis::Adapters::TaLib::Analyzer& analyzer, Didrachma::Studio::Core::EventList& events,
                        YahooHistoryLoader& loader, std::span<const Market::Core::Series::Bar> demo_bars,
                        std::string& selected_instance, std::string& selected_profile, std::string& status,
                        Didrachma::Studio::Core::WorkspaceRepository& repository) {
    static char profile_name[64] = "My profile";
    static char renamed_profile[64] = "Renamed profile";
    const auto persist = [&] {
        if (const auto error = repository.save(workspace)) {
            status = error->message;
            return false;
        }

        return true;
    };

    ImGui::Begin("Profiles", &open);
    ImGui::InputText("Name", profile_name, sizeof(profile_name));
    ImGui::InputText("Rename to", renamed_profile, sizeof(renamed_profile));
    if (auto* document = workspace.selected_chart()) {
        if (ImGui::Button("Create")) {
            if (const auto error = workspace.profiles().add(Apps::Studio::capture_profile(*document, profile_name)))
                status = error->message;
            else {
                selected_profile = profile_name;
                if (persist())
                    status = "Profile created and saved";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Update")) {
            const auto name = selected_profile.empty() ? std::string{profile_name} : selected_profile;
            if (const auto error = workspace.profiles().update(name, Apps::Studio::capture_profile(*document, name)))
                status = error->message;
            else if (persist())
                status = "Profile updated from the selected chart and saved";
        }
    }

    for (const auto& profile : workspace.profiles().profiles()) {
        ImGui::PushID(profile.name.c_str());
        const auto label = profile.name + (workspace.profiles().default_name() == profile.name ? " (default)" : "");
        if (ImGui::Selectable(label.c_str(), selected_profile == profile.name))
            selected_profile = profile.name;
        ImGui::SameLine();
        if (ImGui::SmallButton("Apply"))
            if (auto* document = workspace.selected_chart()) {
                if (const auto error = StockChart::Core::apply_profile(profile, *document))
                    status = error->message;
                else {
                    selected_profile = profile.name;
                    selected_instance.clear();
                    const auto view = std::ranges::find(views, document->id(), &ChartView::id);
                    if (view != views.end())
                        update_geometry(*view, *document, analyzer, &events);
                    if (persist())
                        status = "Profile applied to chart runtime and saved";
                }
            }
        ImGui::SameLine();
        if (ImGui::SmallButton("Default"))
            if (const auto error = workspace.profiles().set_default(profile.name))
                status = error->message;
            else if (persist()) {
                selected_profile = profile.name;
                status = "Default profile saved";
            }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            if (!workspace.profiles().remove(profile.name))
                status = "Profile does not exist";
            else if (persist())
                status = "Profile deleted and workspace saved";
            if (selected_profile == profile.name)
                selected_profile.clear();
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }

    if (ImGui::Button("Rename selected") && !selected_profile.empty()) {
        if (const auto error = workspace.profiles().rename(selected_profile, renamed_profile))
            status = error->message;
        else {
            selected_profile = renamed_profile;
            if (persist())
                status = "Profile renamed and workspace saved";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save workspace")) {
        if (const auto error = repository.save(workspace))
            status = error->message;
        else
            status = "Workspace saved";
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore workspace")) {
        auto loaded = repository.load();
        if (auto* restored = std::get_if<Didrachma::Studio::Core::Workspace>(&loaded)) {
            workspace = std::move(*restored);
            selected_instance.clear();
            selected_profile = workspace.profiles().default_name().value_or(std::string{});
            restore_workspace_runtime(workspace, views, analyzer, events, loader, demo_bars);
            status = "Workspace restored";
        } else
            status = std::get<Didrachma::Studio::Core::WorkspaceError>(loaded).message;
    }
    ImGui::End();
}
} // namespace Didrachma::Apps::Studio
