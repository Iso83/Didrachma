#include <Didrachma/studio/core/BacktestSetup.h>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>

namespace Didrachma::Studio::Core {
namespace Intern {
std::optional<Market::Core::Time::UtcTimestamp> parse_date(std::string_view text, bool through) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-')
        return {};

    int year{}, month{}, day{};
    const auto digits = [&](std::size_t begin, std::size_t count, int& value) {
        value = 0;
        for (std::size_t index = begin; index < begin + count; ++index) {
            if (text[index] < '0' || text[index] > '9')
                return false;

            value = value * 10 + text[index] - '0';
        }
        return true;
    };

    if (!digits(0, 4, year) || !digits(5, 2, month) || !digits(8, 2, day))
        return {};

    const std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
                                           std::chrono::day{static_cast<unsigned>(day)}};
    if (!date.ok())
        return {};

    auto result = Market::Core::Time::UtcTimestamp{std::chrono::sys_days{date}.time_since_epoch()};
    if (through)
        result += std::chrono::hours{24} - std::chrono::seconds{1};
    return result;
}

Strategy::Core::BacktestError field_error(std::string path, std::string message) {
    return {Strategy::Core::BacktestErrorCode::InvalidRequest, std::move(path), std::move(message)};
}
} // namespace Intern

BacktestSetup::BacktestSetup(Strategy::Core::Definition definition, BacktestSetupValues values)
    : m_definition(std::move(definition)), m_values(std::move(values)) {
    if (!requires_subject())
        m_values.subject.clear();
}

bool BacktestSetup::requires_subject() const {
    return std::ranges::any_of(m_definition.series, [](const auto& series) {
        return series.instrument.kind == Strategy::Core::InstrumentKind::Subject;
    });
}

BacktestSetupValidation BacktestSetup::validate(Analysis::Core::Indicator::Analyzer& analyzer,
                                                std::span<const Strategy::Core::BacktestSourceCapability> capabilities,
                                                Market::Core::Time::UtcTimestamp now,
                                                bool definition_uncommitted) const {
    BacktestSetupValidation result;
    if (definition_uncommitted)
        result.errors.push_back(Intern::field_error(
            "/definition", "The strategy has dirty or applied-but-unsaved changes; save it and reopen Run Setup"));
    const auto from = Intern::parse_date(m_values.from_date, false);
    const auto through = Intern::parse_date(m_values.through_date, true);
    if (!from)
        result.errors.push_back(Intern::field_error("/from", "Use a valid UTC date such as 2025-01-31"));
    if (!through)
        result.errors.push_back(Intern::field_error("/through", "Use a valid UTC date such as 2025-01-31"));
    if (!from || !through)
        return result;

    Strategy::Core::BacktestRequest request{
        m_definition,
        !requires_subject() || m_values.subject.empty() ? std::nullopt : std::optional{m_values.subject},
        *from,
        *through,
        {m_values.provider_id, {}},
        m_values.execution,
        m_values.fill_model_version};
    result.preview = Strategy::Core::plan_backtest(request, analyzer, capabilities, now);
    result.errors.insert(result.errors.end(), result.preview.errors.begin(), result.preview.errors.end());
    if (result.errors.empty())
        result.request = std::move(request);
    return result;
}

bool BacktestSetup::submit(Analysis::Core::Indicator::Analyzer& analyzer,
                           std::span<const Strategy::Core::BacktestSourceCapability> capabilities,
                           Market::Core::Time::UtcTimestamp now, bool definition_uncommitted) {
    auto checked = validate(analyzer, capabilities, now, definition_uncommitted);
    if (!checked.request) {
        m_submission.reset();
        return false;
    }

    m_submission = std::move(*checked.request);
    return true;
}

std::vector<Strategy::Core::RepositoryError>
RecentBacktestSetupRepository::save(const BacktestSetupValues& values) const {
    nlohmann::json json{{"formatVersion", 1},
                        {"subject", values.subject},
                        {"fromDate", values.from_date},
                        {"throughDate", values.through_date},
                        {"providerId", values.provider_id},
                        {"quantity", values.execution.quantity},
                        {"startingCapital", values.execution.starting_capital},
                        {"fixedPerFill", values.execution.fixed_per_fill},
                        {"percentagePerFill", values.execution.percentage_per_fill},
                        {"slippagePercentage", values.execution.slippage_percentage},
                        {"fillModelVersion", values.fill_model_version}};
    std::ofstream output(m_path);
    if (!output)
        return {{"/", "Cannot open recent backtest setup file for writing"}};

    output << std::setw(2) << json << '\n';
    return output ? std::vector<Strategy::Core::RepositoryError>{}
                  : std::vector<Strategy::Core::RepositoryError>{{"/", "Cannot write recent backtest setup file"}};
}

RecentBacktestSetupLoad RecentBacktestSetupRepository::load() const {
    try {
        std::ifstream input(m_path);
        if (!input)
            return std::vector<Strategy::Core::RepositoryError>{{"/", "Cannot open recent backtest setup file"}};

        const auto json = nlohmann::json::parse(input);
        if (json.at("formatVersion").get<int>() != 1)
            return std::vector<Strategy::Core::RepositoryError>{{"/formatVersion", "Unsupported setup format version"}};

        BacktestSetupValues values;
        values.subject = json.at("subject").get<std::string>();
        values.from_date = json.at("fromDate").get<std::string>();
        values.through_date = json.at("throughDate").get<std::string>();
        values.provider_id = json.at("providerId").get<std::string>();
        values.execution.quantity = json.at("quantity").get<double>();
        if (!json.at("startingCapital").is_null())
            values.execution.starting_capital = json.at("startingCapital").get<double>();
        values.execution.fixed_per_fill = json.at("fixedPerFill").get<double>();
        values.execution.percentage_per_fill = json.at("percentagePerFill").get<double>();
        values.execution.slippage_percentage = json.at("slippagePercentage").get<double>();
        values.fill_model_version = json.at("fillModelVersion").get<std::uint32_t>();
        return values;
    } catch (const std::exception& error) {
        return std::vector<Strategy::Core::RepositoryError>{{"/", error.what()}};
    }
}
} // namespace Didrachma::Studio::Core
