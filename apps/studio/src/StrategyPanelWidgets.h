#pragma once

#include <Didrachma/analysis/core/indicator/Definition.h>
#include <Didrachma/strategy/core/Model.h>
#include <Didrachma/strategy/core/Repository.h>
#include <Didrachma/studio/core/StrategyEditor.h>
#include <span>

namespace Didrachma::Apps::Studio::Intern {
void text_input(const char*, std::string&);
void price_policy(const char*, Strategy::Core::PricePolicy&, const Strategy::Core::Definition&,
                  std::span<const Analysis::Core::Indicator::Definition>, Didrachma::Studio::Core::StrategyEditor&,
                  std::string_view, const std::vector<Strategy::Core::RepositoryError>&);
void condition_editor(const char*, std::shared_ptr<Strategy::Core::ConditionExpression>&,
                      const Strategy::Core::Definition&, std::span<const Analysis::Core::Indicator::Definition>,
                      Didrachma::Studio::Core::StrategyEditor&, std::string_view,
                      const std::vector<Strategy::Core::RepositoryError>&, int depth = 0);
} // namespace Didrachma::Apps::Studio::Intern
