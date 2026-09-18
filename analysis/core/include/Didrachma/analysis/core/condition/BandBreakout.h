#pragma once

#include <Didrachma/analysis/core/condition/Condition.h>

namespace Didrachma::Analysis::Core::Condition {
class BandBreakout final : public Definition {
    std::string m_id{"bollinger-breakout"};

public:
    [[nodiscard]] const std::string& id() const override {
        return m_id;
    }
    [[nodiscard]] std::vector<Event> evaluate(const Inputs& inputs) const override;
};
} // namespace Didrachma::Analysis::Core::Condition
