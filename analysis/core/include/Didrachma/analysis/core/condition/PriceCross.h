#pragma once

#include <Didrachma/analysis/core/condition/Condition.h>

namespace Didrachma::Analysis::Core::Condition {
class PriceCross final : public Definition {
    std::string m_id{"price-cross-moving-average"};

public:
    [[nodiscard]] const std::string& id() const override {
        return m_id;
    }
    [[nodiscard]] std::vector<Event> evaluate(const Inputs& inputs) const override;
};
} // namespace Didrachma::Analysis::Core::Condition
