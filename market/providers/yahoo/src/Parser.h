#pragma once

#include <Didrachma/market/core/series/Bar.h>
#include <Didrachma/market/core/time/Frame.h>
#include <nlohmann/json_fwd.hpp>
#include <stdexcept>
#include <vector>

namespace Didrachma::Market::Providers::Yahoo::Intern {
class ResponseError : public std::runtime_error {
    std::string m_code;

public:
    ResponseError(std::string code, std::string description)
        : std::runtime_error("Yahoo Finance " + code + ": " + description), m_code(std::move(code)) {}

    [[nodiscard]] const std::string& code() const {
        return m_code;
    }
};

std::vector<Core::Series::Bar> parse_chart(const nlohmann::json& json, Core::Time::Frame timeframe);
std::vector<Core::Series::Bar> parse_history(const nlohmann::json& json, Core::Time::Frame timeframe);
} // namespace Didrachma::Market::Providers::Yahoo::Intern
