#pragma once

#include <Didrachma/market/core/provider/History.h>
#include <Didrachma/stockChart/core/Profile.h>
#include <filesystem>
#include <set>
#include <variant>

namespace Didrachma::Studio::Core {
inline constexpr std::uint32_t WorkspaceFormatVersion = 2;

struct WorkspaceError {
    std::string message;
};

class Workspace {
    StockChart::Core::ProfileCollection m_profiles;
    std::vector<StockChart::Core::Document> m_documents;
    std::map<std::string, Market::Core::Provider::HistoryRequest> m_history_requests;
    std::set<std::string> m_polling_charts;
    std::optional<std::string> m_selected_chart_id;
    std::uint64_t m_next_chart_id{1};

public:
    [[nodiscard]] StockChart::Core::ProfileCollection& profiles() {
        return m_profiles;
    }
    [[nodiscard]] const StockChart::Core::ProfileCollection& profiles() const {
        return m_profiles;
    }
    [[nodiscard]] std::span<StockChart::Core::Document> documents() {
        return m_documents;
    }
    [[nodiscard]] std::span<const StockChart::Core::Document> documents() const {
        return m_documents;
    }
    [[nodiscard]] const std::optional<std::string>& selected_chart_id() const {
        return m_selected_chart_id;
    }

    StockChart::Core::Document& create_chart(Market::Core::Series::Key series, Market::Core::Time::Range visible_range,
                                             std::optional<Market::Core::Time::Range> history_range = std::nullopt);
    bool close_chart(const std::string& chart_id);
    bool select_chart(const std::string& chart_id);
    bool set_polling(const std::string& chart_id, bool enabled);
    [[nodiscard]] bool polling(const std::string& chart_id) const {
        return m_polling_charts.contains(chart_id);
    }
    [[nodiscard]] StockChart::Core::Document* selected_chart();
    [[nodiscard]] const Market::Core::Provider::HistoryRequest* history_request(const std::string& chart_id) const;

    friend std::string serialize_workspace(const Workspace& workspace);
    friend std::variant<Workspace, WorkspaceError> deserialize_workspace(const std::string& text);
};

std::string serialize_workspace(const Workspace& workspace);
std::variant<Workspace, WorkspaceError> deserialize_workspace(const std::string& text);

class WorkspaceRepository {
    std::filesystem::path m_path;

public:
    explicit WorkspaceRepository(std::filesystem::path path) : m_path(std::move(path)) {}
    [[nodiscard]] std::variant<Workspace, WorkspaceError> load() const;
    [[nodiscard]] std::optional<WorkspaceError> save(const Workspace& workspace) const;
};
} // namespace Didrachma::Studio::Core
