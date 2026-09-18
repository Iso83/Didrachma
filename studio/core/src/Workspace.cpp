#include <Didrachma/studio/core/Workspace.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace Didrachma::Studio::Core {
namespace Intern {
using Json = nlohmann::json;

std::int64_t seconds(Market::Core::Time::UtcTimestamp value) {
    return std::chrono::duration_cast<std::chrono::seconds>(value.time_since_epoch()).count();
}

Json parameter(const Analysis::Core::Indicator::ParameterValue& value) {
    return std::visit([](const auto& item) { return Json(item); }, value);
}

Analysis::Core::Indicator::ParameterValue parameter(const Json& value) {
    if (value.is_boolean())
        return value.get<bool>();

    if (value.is_number_integer())
        return value.get<std::int64_t>();

    if (value.is_number())
        return value.get<double>();

    return value.get<std::string>();
}
} // namespace Intern

StockChart::Core::Document& Workspace::create_chart(Market::Core::Series::Key series,
                                                    Market::Core::Time::Range visible_range,
                                                    std::optional<Market::Core::Time::Range> history_range) {
    const auto id = "chart-" + std::to_string(m_next_chart_id++);
    m_history_requests.emplace(id,
                               Market::Core::Provider::HistoryRequest{series, history_range.value_or(visible_range)});
    m_documents.emplace_back(id, std::move(series), visible_range);
    auto& document = m_documents.back();
    if (const auto& default_name = m_profiles.default_name(); default_name)
        StockChart::Core::apply_profile(*m_profiles.find(*default_name), document);
    m_selected_chart_id = id;
    return document;
}

bool Workspace::close_chart(const std::string& chart_id) {
    if (!std::erase_if(m_documents, [&](const auto& document) { return document.id() == chart_id; }))
        return false;

    if (m_selected_chart_id == chart_id)
        m_selected_chart_id = m_documents.empty() ? std::nullopt : std::optional{m_documents.front().id()};

    m_history_requests.erase(chart_id);
    m_polling_charts.erase(chart_id);
    return true;
}

bool Workspace::select_chart(const std::string& chart_id) {
    if (std::ranges::find(m_documents, chart_id, &StockChart::Core::Document::id) == m_documents.end())
        return false;

    m_selected_chart_id = chart_id;
    return true;
}

bool Workspace::set_polling(const std::string& chart_id, bool enabled) {
    if (!history_request(chart_id))
        return false;

    if (enabled)
        m_polling_charts.insert(chart_id);
    else
        m_polling_charts.erase(chart_id);
    return true;
}

StockChart::Core::Document* Workspace::selected_chart() {
    if (!m_selected_chart_id)
        return nullptr;

    const auto found = std::ranges::find(m_documents, *m_selected_chart_id, &StockChart::Core::Document::id);
    return found == m_documents.end() ? nullptr : &*found;
}

const Market::Core::Provider::HistoryRequest* Workspace::history_request(const std::string& chart_id) const {
    const auto found = m_history_requests.find(chart_id);
    return found == m_history_requests.end() ? nullptr : &found->second;
}

std::string serialize_workspace(const Workspace& workspace) {
    using namespace Intern;
    Json charts = Json::array();
    for (const auto& document : workspace.m_documents) {
        const auto& key = document.series();
        Json indicators = Json::array(), layers = Json::array();
        for (const auto& entry : document.indicators()) {
            Json parameters = Json::object();
            for (const auto& [name, value] : entry.instance.parameters)
                parameters[name] = parameter(value);

            indicators.push_back({{"id", entry.instance.id},
                                  {"definitionId", entry.instance.definition_id},
                                  {"name", entry.name},
                                  {"enabled", entry.instance.enabled},
                                  {"parameters", std::move(parameters)}});
        }
        for (const auto& layer : document.layers()) {
            Json outputs = Json::array();
            for (const auto& output : layer.outputs)
                outputs.push_back({{"instanceId", output.instance_id}, {"outputId", output.output_id}});

            layers.push_back(
                {{"kind", static_cast<int>(layer.kind)},
                 {"outputs", std::move(outputs)},
                 {"color",
                  {layer.style.color.red, layer.style.color.green, layer.style.color.blue, layer.style.color.alpha}},
                 {"visible", layer.style.visible},
                 {"lineWidth", layer.style.line_width},
                 {"bandFillOpacity", layer.style.band_fill_opacity},
                 {"pane", static_cast<int>(layer.pane)}});
        }
        charts.push_back({{"id", document.id()},
                          {"provider", key.provider},
                          {"instrument", key.instrument},
                          {"frameQuantity", key.timeframe.quantity},
                          {"frameUnit", static_cast<int>(key.timeframe.unit)},
                          {"visibleBegin", seconds(document.visible_range().begin)},
                          {"visibleEnd", seconds(document.visible_range().end)},
                          {"historyBegin", seconds(workspace.m_history_requests.at(document.id()).range.begin)},
                          {"historyEnd", seconds(workspace.m_history_requests.at(document.id()).range.end)},
                          {"polling", workspace.polling(document.id())},
                          {"autoFollow", document.auto_follow()},
                          {"pricePaneRatio", document.price_pane_ratio()},
                          {"separatePaneRatio", document.separate_pane_ratio()},
                          {"showHoverValues", document.show_hover_values()},
                          {"indicators", std::move(indicators)},
                          {"layers", std::move(layers)}});
    }
    Json root{{"version", WorkspaceFormatVersion},
              {"profiles", Json::parse(StockChart::Core::serialize_profiles(workspace.m_profiles))},
              {"charts", std::move(charts)},
              {"nextChartId", workspace.m_next_chart_id}};
    root["selectedChartId"] = workspace.m_selected_chart_id ? Json(*workspace.m_selected_chart_id) : Json(nullptr);

    return root.dump(2);
}

std::variant<Workspace, WorkspaceError> deserialize_workspace(const std::string& text) {
    using namespace Intern;
    try {
        const auto root = Json::parse(text);
        const auto version = root.at("version").get<std::uint32_t>();
        if (version != WorkspaceFormatVersion)
            return WorkspaceError{version == 1 ? "Workspace format version 1 has no provider history request; "
                                                 "migration cannot safely reopen charts"
                                               : "Unsupported workspace format version " + std::to_string(version)};

        auto decoded_profiles = StockChart::Core::deserialize_profiles(root.at("profiles").dump());
        if (const auto* error = std::get_if<StockChart::Core::ProfileError>(&decoded_profiles))
            return WorkspaceError{error->message};

        Workspace workspace;
        workspace.m_profiles = std::get<StockChart::Core::ProfileCollection>(std::move(decoded_profiles));
        workspace.m_next_chart_id = root.at("nextChartId").get<std::uint64_t>();
        for (const auto& chart : root.at("charts")) {
            Market::Core::Series::Key key{chart.at("provider").get<std::string>(),
                                          chart.at("instrument").get<std::string>(),
                                          {chart.at("frameQuantity").get<std::uint32_t>(),
                                           static_cast<Market::Core::Time::Unit>(chart.at("frameUnit").get<int>())}};
            const auto begin =
                Market::Core::Time::UtcTimestamp{std::chrono::seconds{chart.at("visibleBegin").get<std::int64_t>()}};
            const auto end =
                Market::Core::Time::UtcTimestamp{std::chrono::seconds{chart.at("visibleEnd").get<std::int64_t>()}};
            workspace.m_documents.emplace_back(chart.at("id").get<std::string>(), std::move(key),
                                               Market::Core::Time::Range{begin, end});
            auto& document = workspace.m_documents.back();
            const auto history_begin =
                Market::Core::Time::UtcTimestamp{std::chrono::seconds{chart.at("historyBegin").get<std::int64_t>()}};
            const auto history_end =
                Market::Core::Time::UtcTimestamp{std::chrono::seconds{chart.at("historyEnd").get<std::int64_t>()}};
            workspace.m_history_requests.emplace(
                document.id(), Market::Core::Provider::HistoryRequest{document.series(), {history_begin, history_end}});
            if (chart.value("polling", false))
                workspace.m_polling_charts.insert(document.id());
            document.set_auto_follow(chart.at("autoFollow").get<bool>());
            document.set_price_pane_ratio(chart.value("pricePaneRatio", 0.76F));
            document.set_separate_pane_ratio(chart.value("separatePaneRatio", 0.72F));
            document.set_show_hover_values(chart.value("showHoverValues", true));
            std::map<std::string, std::string> instance_ids;
            for (const auto& encoded : chart.at("indicators")) {
                std::map<std::string, Analysis::Core::Indicator::ParameterValue> parameters;
                for (const auto& [name, value] : encoded.at("parameters").items())
                    parameters.emplace(name, parameter(value));

                const auto id =
                    document.add_indicator(encoded.at("definitionId").get<std::string>(), std::move(parameters));
                document.set_indicator_enabled(id, encoded.at("enabled").get<bool>());
                document.set_indicator_name(id, encoded.value("name", encoded.at("definitionId").get<std::string>()));
                instance_ids.emplace(encoded.at("id").get<std::string>(), id);
            }
            for (const auto& encoded : chart.at("layers")) {
                std::vector<StockChart::Core::OutputBinding> outputs;
                for (const auto& output : encoded.at("outputs"))
                    outputs.push_back({instance_ids.at(output.at("instanceId").get<std::string>()),
                                       output.at("outputId").get<std::string>()});

                const auto color = encoded.at("color").get<std::vector<float>>();
                StockChart::Core::VisualStyle style{{color.at(0), color.at(1), color.at(2), color.at(3)},
                                                    encoded.at("visible").get<bool>(),
                                                    encoded.at("lineWidth").get<float>(),
                                                    encoded.at("bandFillOpacity").get<float>()};
                document.add_layer(static_cast<StockChart::Core::LayerKind>(encoded.at("kind").get<int>()),
                                   std::move(outputs), style,
                                   static_cast<StockChart::Core::LayerPane>(
                                       encoded.value("pane", static_cast<int>(StockChart::Core::LayerPane::Price))));
            }
        }
        if (!root.at("selectedChartId").is_null())
            workspace.m_selected_chart_id = root.at("selectedChartId").get<std::string>();
        if (workspace.m_selected_chart_id && !workspace.select_chart(*workspace.m_selected_chart_id))
            return WorkspaceError{"Selected chart does not exist"};

        return workspace;
    } catch (const std::exception& error) {
        return WorkspaceError{error.what()};
    }
}

std::variant<Workspace, WorkspaceError> WorkspaceRepository::load() const {
    std::ifstream input(m_path);
    if (!input)
        return WorkspaceError{"Unable to open workspace"};

    std::ostringstream text;
    text << input.rdbuf();
    return deserialize_workspace(text.str());
}

std::optional<WorkspaceError> WorkspaceRepository::save(const Workspace& workspace) const {
    std::ofstream output(m_path, std::ios::trunc);
    if (!output || !(output << serialize_workspace(workspace)))
        return WorkspaceError{"Unable to save workspace"};

    return std::nullopt;
}
} // namespace Didrachma::Studio::Core
