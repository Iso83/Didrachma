#pragma once

#include <Didrachma/strategy/core/Model.h>
#include <set>

namespace Didrachma::Studio::Core::Intern {
Strategy::Core::Definition clone_definition(const Strategy::Core::Definition&);
void condition_ids(const std::shared_ptr<Strategy::Core::ConditionExpression>&, std::set<std::string>&);
void all_condition_references(const Strategy::Core::Definition&, std::string_view series, std::string_view indicator,
                              std::vector<std::string>&);
std::string frame_label(Market::Core::Time::Frame);
} // namespace Didrachma::Studio::Core::Intern
