#include "ChartOverlay.h"
#include "ChartView.h"
#include "DemoData.h"
#include "Docking.h"
#include "EventPanel.h"
#include "IndicatorActions.h"
#include "Performance.h"
#include "ProfilePanel.h"
#include "RuntimeOptions.h"
#include "WorkspaceRuntime.h"
#include "YahooChartDialog.h"

#include <Didrachma/analysis/adapters/talib/Analyzer.h>
#include <Didrachma/analysis/core/indicator/Validation.h>
#include <Didrachma/stockChart/render/CanvasHost.h>
#include <Didrachma/studio/core/Workspace.h>
#include <GLFW/glfw3.h>
#include <ScopeCanvas/integration/imgui/DisplayScale.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <glad/glad.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <memory>
#include <set>
#include <string_view>
#include <vector>

using Didrachma::StockChart::Render::CanvasHost;
namespace {
using namespace Didrachma::StockChart::Render;
using Bar = Didrachma::Market::Core::Series::Bar;
using Didrachma::Apps::Studio::ChartView;
using Didrachma::Apps::Studio::timeframe_label;
using Didrachma::Apps::Studio::update_geometry;
namespace ChartCore = Didrachma::StockChart::Core;
namespace Indicator = Didrachma::Analysis::Core::Indicator;
namespace Studio = Didrachma::Studio::Core;
using Timestamp = Didrachma::Market::Core::Time::UtcTimestamp;

struct PlatformPointer {
    ImVec2 position;
    bool over_window{};
    GLFWwindow* window{};
    bool detached{};
};

PlatformPointer platform_pointer() {
    const auto* viewport = ImGui::GetWindowViewport();
    auto* window = viewport ? static_cast<GLFWwindow*>(viewport->PlatformHandle) : nullptr;
    if (!window)
        return {ImGui::GetMousePos(), ImGui::IsWindowHovered(), nullptr, false};

    double cursor_x{}, cursor_y{};
    int window_x{}, window_y{};
    glfwGetCursorPos(window, &cursor_x, &cursor_y);
    glfwGetWindowPos(window, &window_x, &window_y);
    return {{static_cast<float>(window_x + cursor_x), static_cast<float>(window_y + cursor_y)},
            glfwGetWindowAttrib(window, GLFW_HOVERED) == GLFW_TRUE,
            window,
            viewport != ImGui::GetMainViewport()};
}

} // namespace

int main(int argc, char** argv) {
    const auto runtime_options = Didrachma::Apps::Studio::parse_runtime_options(argc, argv);
    Didrachma::Apps::Studio::apply_laptop_gpu_preference(runtime_options.discrete_gpu);
    if (!glfwInit())
        return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Didrachma Studio", nullptr, nullptr);
    if (!window)
        return 1;
    GLFWcursor* pane_resize_cursor = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);

    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        return 1;

    glfwSwapInterval(runtime_options.vsync ? 1 : 0);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& imgui_io = ImGui::GetIO();
    imgui_io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    imgui_io.ConfigViewportsNoDecoration = false;
    imgui_io.ConfigWindowsMoveFromTitleBarOnly = true;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    ScopeCanvas::Integration::ImGui::DisplayScale display_scale;
    display_scale.update(window);

    Studio::Workspace workspace;
    const auto range = Didrachma::Market::Core::Time::Range{Timestamp{std::chrono::seconds{1700000000}},
                                                            Timestamp{std::chrono::seconds{1700086400}}};
    std::vector<ChartView> views;

    Didrachma::Analysis::Adapters::TaLib::Analyzer analyzer;
    const auto catalog = analyzer.catalog();
    ImGuiTextFilter catalog_filter;
    std::string selected_instance;
    char instance_name[96]{};
    std::string status;
    std::string selected_profile;
    Studio::WorkspaceRepository repository{"didrachma-workspace.json"};
    Didrachma::Apps::Studio::YahooHistoryLoader yahoo_history;
    Studio::EventList analysis_events;
    std::set<std::string> highlighted_event_ids;
    using namespace Didrachma::StockChart::Render;
    const auto bars = Didrachma::Apps::Studio::demo_bars();

    if (std::filesystem::exists("didrachma-workspace.json")) {
        auto loaded = repository.load();
        if (auto* restored = std::get_if<Studio::Workspace>(&loaded)) {
            workspace = std::move(*restored);
            if (workspace.profiles().default_name())
                selected_profile = *workspace.profiles().default_name();
            Didrachma::Apps::Studio::restore_workspace_runtime(workspace, views, analyzer, analysis_events,
                                                               yahoo_history, bars);
            status = "Workspace restored from previous session";
        } else {
            status = "Previous workspace could not be restored: " + std::get<Studio::WorkspaceError>(loaded).message;
            Didrachma::Apps::Studio::initialize_demo_charts(workspace, views, analyzer, analysis_events, bars, range);
        }
    } else
        Didrachma::Apps::Studio::initialize_demo_charts(workspace, views, analyzer, analysis_events, bars, range);

    bool show_catalog = true;
    bool show_instances = true;
    bool show_profiles = true;
    bool show_events = true;
    bool show_yahoo_chart = false;
    bool show_render_diagnostics = runtime_options.render_diagnostics;

    while (!glfwWindowShouldClose(window)) {
        glfwWaitEventsTimeout(1.0 / 30.0);
        display_scale.update(window);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        yahoo_history.apply_if_ready(workspace, views, analyzer, analysis_events, status);

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Chart")) {
                if (ImGui::BeginMenu("Provider")) {
                    if (ImGui::MenuItem("Yahoo Finance...", nullptr, false, !yahoo_history.busy()))
                        show_yahoo_chart = true;
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Indicator Catalog", nullptr, &show_catalog);
                ImGui::MenuItem("Indicator Instances", nullptr, &show_instances);
                ImGui::MenuItem("Profiles", nullptr, &show_profiles);
                ImGui::MenuItem("Analysis Events", nullptr, &show_events);
                ImGui::MenuItem("Render diagnostics", nullptr, &show_render_diagnostics);
                if (ImGui::BeginMenu("StockCharts")) {
                    for (auto& view : views)
                        ImGui::MenuItem(view.id.c_str(), nullptr, &view.open);
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        const auto dockspace = ImGui::DockSpaceOverViewport();
        const auto chart_dock = Didrachma::Apps::Studio::configure_default_docking(dockspace);

        if (const auto request = Didrachma::Apps::Studio::draw_yahoo_chart_dialog(show_yahoo_chart, status)) {
            Didrachma::Apps::Studio::open_yahoo_chart(workspace, views, yahoo_history, *request);
        }

        if (show_catalog) {
            ImGui::Begin("Indicator Catalog", &show_catalog);
            if (auto* document = workspace.selected_chart()) {
                catalog_filter.Draw("Search");
                std::string_view group;
                for (const auto& definition : catalog) {
                    if (!catalog_filter.PassFilter(definition.display_name.c_str()) &&
                        !catalog_filter.PassFilter(definition.id.c_str()))
                        continue;

                    if (definition.group != group) {
                        group = definition.group;
                        ImGui::SeparatorText(definition.group.c_str());
                    }
                    if (ImGui::Selectable(definition.display_name.c_str())) {
                        selected_instance = Didrachma::Apps::Studio::add_indicator(*document, definition);
                        std::snprintf(instance_name, sizeof(instance_name), "%s", definition.display_name.c_str());
                        const auto view = std::ranges::find(views, document->id(), &ChartView::id);
                        if (view != views.end())
                            update_geometry(*view, *document, analyzer, &analysis_events);
                    }
                }
            } else
                ImGui::TextDisabled("Select a StockChart");
            ImGui::End();
        }

        if (show_instances) {
            ImGui::Begin("Indicator Instances", &show_instances);
            if (auto* document = workspace.selected_chart()) {
                for (std::size_t index = 0; index < document->indicators().size(); ++index) {
                    const auto instance_id = document->indicators()[index].instance.id;
                    bool enabled = document->indicators()[index].instance.enabled;
                    ImGui::PushID(instance_id.c_str());
                    if (ImGui::Checkbox("##enabled", &enabled)) {
                        document->set_indicator_enabled(instance_id, enabled);
                        const auto view = std::ranges::find(views, document->id(), &ChartView::id);
                        if (view != views.end())
                            update_geometry(*view, *document, analyzer, &analysis_events);
                    }
                    ImGui::SameLine();
                    if (ImGui::Selectable(document->indicators()[index].name.c_str(),
                                          selected_instance == instance_id)) {
                        selected_instance = instance_id;
                        std::snprintf(instance_name, sizeof(instance_name), "%s",
                                      document->indicators()[index].name.c_str());
                    }
                    ImGui::PopID();
                }
                const auto selected = std::ranges::find_if(
                    document->indicators(), [&](const auto& item) { return item.instance.id == selected_instance; });
                if (selected != document->indicators().end()) {
                    const auto index =
                        static_cast<std::size_t>(std::distance(document->indicators().begin(), selected));
                    ImGui::Separator();
                    ImGui::BeginDisabled(index == 0);
                    const bool move_up = ImGui::Button("Move up");
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(index + 1 == document->indicators().size());
                    const bool move_down = ImGui::Button("Move down");
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    const bool remove = ImGui::Button("Remove selected");
                    if (move_up || move_down) {
                        document->move_indicator(selected_instance, move_up ? index - 1 : index + 1);
                        const auto view = std::ranges::find(views, document->id(), &ChartView::id);
                        if (view != views.end())
                            update_geometry(*view, *document, analyzer, &analysis_events);
                    } else if (remove) {
                        document->remove_indicator(selected_instance);
                        selected_instance.clear();
                        const auto view = std::ranges::find(views, document->id(), &ChartView::id);
                        if (view != views.end())
                            update_geometry(*view, *document, analyzer, &analysis_events);
                    }
                }
                if (auto* entry = document->find_indicator(selected_instance)) {
                    const bool rename_entered = ImGui::InputText("Name", instance_name, sizeof(instance_name),
                                                                 ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::SameLine();
                    if (rename_entered || ImGui::Button("Rename"))
                        document->set_indicator_name(selected_instance, instance_name);
                    const auto definition =
                        std::ranges::find(catalog, entry->instance.definition_id, &Indicator::Definition::id);
                    if (definition != catalog.end())
                        for (const auto& metadata : definition->parameters) {
                            auto parameters = entry->instance.parameters;
                            bool changed = false;
                            if (metadata.kind == Indicator::ParameterKind::Integer) {
                                int value = static_cast<int>(std::get<std::int64_t>(parameters[metadata.id]));
                                changed = ImGui::InputInt(metadata.display_name.c_str(), &value);
                                if (changed)
                                    parameters[metadata.id] = std::int64_t{value};
                            } else if (metadata.kind == Indicator::ParameterKind::Real) {
                                double value = std::get<double>(parameters[metadata.id]);
                                changed = ImGui::InputDouble(metadata.display_name.c_str(), &value);
                                if (changed)
                                    parameters[metadata.id] = value;
                            }
                            if (changed) {
                                if (const auto error = Indicator::validate_parameters(*definition, parameters))
                                    status = error->message;
                                else {
                                    document->set_indicator_parameters(selected_instance, std::move(parameters));
                                    status.clear();
                                    const auto view = std::ranges::find(views, document->id(), &ChartView::id);
                                    if (view != views.end())
                                        update_geometry(*view, *document, analyzer, &analysis_events);
                                }
                            }
                        }
                    bool style_changed = false;
                    for (const auto& layer : document->layers())
                        if (!layer.outputs.empty() && layer.outputs[0].instance_id == selected_instance) {
                            auto style = layer.style;
                            float color[4]{style.color.red, style.color.green, style.color.blue, style.color.alpha};
                            ImGui::PushID(layer.id.c_str());
                            bool changed = ImGui::ColorEdit4("Output color", color);
                            changed |= ImGui::SliderFloat("Line width", &style.line_width, 0.5F, 8.0F);
                            if (layer.kind == ChartCore::LayerKind::Band)
                                changed |= ImGui::SliderFloat("Fill opacity", &style.band_fill_opacity, 0, 1);
                            if (changed) {
                                style.color = {color[0], color[1], color[2], color[3]};
                                document->set_layer_style(layer.id, style);
                                style_changed = true;
                            }
                            ImGui::PopID();
                        }
                    if (style_changed)
                        if (const auto view = std::ranges::find(views, document->id(), &ChartView::id);
                            view != views.end())
                            update_geometry(*view, *document, analyzer, &analysis_events);
                }
                if (!status.empty())
                    ImGui::TextColored({1, 0.4F, 0.4F, 1}, "%s", status.c_str());
            }
            ImGui::End();
        }

        if (show_profiles)
            Didrachma::Apps::Studio::draw_profile_panel(show_profiles, workspace, views, analyzer, analysis_events,
                                                        yahoo_history, bars, selected_instance, selected_profile,
                                                        status, repository);

        if (show_events)
            if (const auto* selected = Didrachma::Apps::Studio::draw_event_panel(
                    analysis_events, workspace.selected_chart(), highlighted_event_ids, &show_events)) {
                if (auto* document = workspace.selected_chart()) {
                    const auto navigation = Studio::navigate_to_event(*document, *selected, range);
                    const auto view = std::ranges::find(views, document->id(), &ChartView::id);
                    if (view != views.end()) {
                        view->visible_range = navigation.revealed_range;
                        update_geometry(*view, *document, analyzer, &analysis_events);
                    }
                    if (navigation.missing_history)
                        status = "Additional history requested for selected event";
                }
            }

        std::vector<GLFWwindow*> detached_chart_windows;
        GLFWwindow* splitter_cursor_window{};
        for (auto& view : views) {
            if (!view.open)
                continue;
            const auto document = std::ranges::find(workspace.documents(), view.id, &ChartCore::Document::id);
            if (document == workspace.documents().end())
                continue;
            const std::string title =
                document->series().instrument + " - " + timeframe_label(document->series().timeframe) + "###" + view.id;
            ImGui::SetNextWindowDockID(chart_dock, ImGuiCond_FirstUseEver);
            ImGui::Begin(title.c_str(), &view.open);
            if (ImGui::IsWindowFocused())
                workspace.select_chart(view.id);
            if (show_render_diagnostics)
                Didrachma::Apps::Studio::draw_performance(view.canvas->counters());
            constexpr float axis_height = 24.0F;
            const auto available = ImGui::GetContentRegionAvail();
            const ImVec2 size{available.x, std::max(1.0F, available.y - axis_height)};
            const Size next_size{static_cast<int>(size.x), static_cast<int>(size.y)};
            if (!(next_size == view.size)) {
                view.size = next_size;
                view.canvas->resize(view.size);
                update_geometry(view, *document, analyzer, &analysis_events);
            }
            const auto panes = view.panes;
            view.canvas->render_if_needed();
            ImGui::Image(static_cast<ImTextureID>(view.canvas->texture()), size, {0, 1}, {1, 0});
            const auto image_minimum = ImGui::GetItemRectMin();
            const auto image_maximum = ImGui::GetItemRectMax();
            const auto pointer = platform_pointer();
            if (pointer.detached && pointer.window &&
                std::ranges::find(detached_chart_windows, pointer.window) == detached_chart_windows.end())
                detached_chart_windows.push_back(pointer.window);
            const auto mouse = pointer.position;
            const bool pointer_over_image = pointer.over_window && mouse.x >= image_minimum.x &&
                                            mouse.x < image_maximum.x && mouse.y >= image_minimum.y &&
                                            mouse.y < image_maximum.y;
            const bool mouse_over_plot = pointer_over_image && mouse.x < image_minimum.x + view.plot_width;
            if (ImGui::BeginPopupContextItem("chart-options")) {
                bool show_hover = document->show_hover_values();
                if (ImGui::Checkbox("Show hover values", &show_hover))
                    document->set_show_hover_values(show_hover);
                ImGui::EndPopup();
            }

            // Resolve the pointer from this chart's GLFW platform window. Detached viewports can otherwise report
            // main-viewport hover coordinates for overlapping items, which makes their splitters unreachable.
            const auto local_y = mouse.y - image_minimum.y;
            const auto near_splitter = [&](int y) {
                return pointer_over_image && mouse.x < image_minimum.x + view.plot_width &&
                       std::abs(local_y - static_cast<float>(y)) <= 5.0F;
            };
            const bool over_price_volume = near_splitter(panes.splitter_y);
            const bool over_indicator_price = panes.separate_height > 0 && near_splitter(panes.separate_splitter_y);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                view.price_volume_resizing = over_price_volume;
                view.indicator_price_resizing = !over_price_volume && over_indicator_price;
            }
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                view.price_volume_resizing = false;
                view.indicator_price_resizing = false;
            }
            if (over_price_volume || over_indicator_price || view.price_volume_resizing ||
                view.indicator_price_resizing) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                if (pointer.detached)
                    splitter_cursor_window = pointer.window;
            }
            const bool price_volume_dragging = view.price_volume_resizing;
            const bool separate_dragging = view.indicator_price_resizing;
            if (price_volume_dragging) {
                document->set_price_pane_ratio(price_pane_ratio_from_splitter(mouse.y - image_minimum.y, view.size));
                view.canvas->invalidation().invalidate(DirtyReason::Size);
                update_geometry(view, *document, analyzer, nullptr);
            } else if (separate_dragging) {
                document->set_separate_pane_ratio(separate_pane_ratio_from_splitter(mouse.y - image_minimum.y, panes));
                view.canvas->invalidation().invalidate(DirtyReason::Size);
                update_geometry(view, *document, analyzer, nullptr);
            }
            if (mouse_over_plot && ImGui::GetIO().MouseWheel != 0) {
                const auto fraction =
                    std::clamp((mouse.x - image_minimum.x) / std::max(view.plot_width, 1), 0.0F, 1.0F);
                const auto duration = view.visible_range.end - view.visible_range.begin;
                const auto anchor =
                    view.visible_range.begin + std::chrono::duration_cast<Timestamp::duration>(duration * fraction);
                view.visible_range =
                    zoom_time_range(view.visible_range, anchor, ImGui::GetIO().MouseWheel,
                                    minimum_zoom_duration(document->series().timeframe), view.history_range);
                document->dispatch(ChartCore::NavigateViewport{view.visible_range});
                update_geometry(view, *document, analyzer, &analysis_events);
            }
            if (mouse_over_plot && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
                view.middle_button_panning = true;
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                view.middle_button_panning = false;
            if (view.middle_button_panning && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                const auto delta = ImGui::GetIO().MouseDelta;
                view.visible_range = pan_time_range(view.visible_range, delta.x, view.plot_width, view.history_range);
                document->dispatch(ChartCore::NavigateViewport{view.visible_range});
                update_geometry(view, *document, analyzer, &analysis_events);
            }
            auto* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddLine({image_minimum.x, image_minimum.y + static_cast<float>(panes.splitter_y)},
                               {image_maximum.x, image_minimum.y + static_cast<float>(panes.splitter_y)},
                               IM_COL32(110, 120, 145, 255), 2.0F);
            if (panes.separate_height > 0)
                draw_list->AddLine({image_minimum.x, image_minimum.y + static_cast<float>(panes.separate_splitter_y)},
                                   {image_maximum.x, image_minimum.y + static_cast<float>(panes.separate_splitter_y)},
                                   IM_COL32(110, 120, 145, 255), 2.0F);
            if (view.has_visible_geometry) {
                const Size price_size{view.plot_width, pane_content_height(panes.price_content_height)};
                const CoordinateMapper selection_mapper{view.visible_range, view.price_range, price_size,
                                                        static_cast<float>(pane_content_top(panes.price_top))};
                for (const auto& event_id : highlighted_event_ids)
                    if (const auto* event = analysis_events.find(event_id);
                        event && event->chart_id == document->id()) {
                        if (event->end) {
                            const auto duration = view.visible_range.end - view.visible_range.begin;
                            const auto x_at = [&](Timestamp timestamp) {
                                const auto offset = timestamp - view.visible_range.begin;
                                return image_minimum.x + view.plot_width * static_cast<float>(offset.count()) /
                                                             static_cast<float>(duration.count());
                            };
                            draw_list->AddRectFilled({x_at(event->start), image_minimum.y},
                                                     {x_at(*event->end), image_maximum.y}, IM_COL32(255, 195, 64, 35));
                        } else
                            for (const auto& segment :
                                 build_event_selection_line(event->start, view.bars, selection_mapper, size.y))
                                draw_list->AddLine(
                                    {image_minimum.x + segment.first.x, image_minimum.y + segment.first.y},
                                    {image_minimum.x + segment.second.x, image_minimum.y + segment.second.y},
                                    IM_COL32(255, 195, 64, 255), 2.0F);
                    }
            }
            Didrachma::Apps::Studio::draw_chart_axes(view, image_minimum, image_maximum);
            if (Didrachma::Apps::Studio::draw_standalone_indicator_tabs(view, *document, image_minimum))
                update_geometry(view, *document, analyzer, &analysis_events);
            if (!view.has_visible_geometry)
                draw_list->AddText({image_minimum.x + 12.0F, image_minimum.y + 82.0F}, IM_COL32(235, 190, 90, 255),
                                   "No bars in the selected history range");
            const Size price_size{view.plot_width, pane_content_height(panes.price_content_height)};
            if (document->show_hover_values() && mouse_over_plot && view.has_visible_geometry &&
                mapping_is_valid(view.visible_range, view.price_range, price_size)) {
                const auto minimum = image_minimum;
                CoordinateMapper mapper{view.visible_range, view.price_range, price_size,
                                        static_cast<float>(pane_content_top(panes.price_top))};
                if (const auto hit = hit_test(view.bars, mapper, {mouse.x - minimum.x, mouse.y - minimum.y})) {
                    const auto& bar = view.bars[hit->index];
                    const auto time = std::chrono::system_clock::to_time_t(bar.open_time);
                    const auto utc = *std::gmtime(&time);
                    char timestamp[32]{};
                    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S UTC", &utc);
                    ImGui::SetTooltip("%s\nO %.2f  H %.2f  L %.2f  C %.2f  V %.0f", timestamp, bar.open, bar.high,
                                      bar.low, bar.close, bar.volume);
                }
            }
            ImGui::End();
        }
        for (auto* chart_window : detached_chart_windows)
            glfwSetCursor(chart_window,
                          chart_window == splitter_cursor_window && pane_resize_cursor ? pane_resize_cursor : nullptr);
        for (auto view = views.begin(); view != views.end();) {
            if (view->open) {
                ++view;
                continue;
            }

            const auto chart_id = view->id;
            yahoo_history.cancel(chart_id);
            analysis_events.clear_chart(chart_id);
            workspace.close_chart(chart_id);
            view = views.erase(view);
            selected_instance.clear();
            status = "Chart closed and runtime state released";
        }
        ImGui::Render();
        int width{}, height{};
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.03F, 0.035F, 0.045F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            auto* main_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(main_context);
        }
        glfwSwapBuffers(window);
    }

    if (const auto error = repository.save(workspace))
        std::fprintf(stderr, "Unable to persist Studio workspace: %s\n", error->message.c_str());

    // Release canvas-owned GL resources while the context is still current, before the UI and window are destroyed.
    views.clear();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyCursor(pane_resize_cursor);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
