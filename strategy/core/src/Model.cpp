#include <Didrachma/strategy/core/Model.h>
#include <unordered_map>

namespace Didrachma::Strategy::Core::Intern {
std::shared_ptr<ConditionExpression>
clone_condition(const std::shared_ptr<ConditionExpression>& source,
                std::unordered_map<const ConditionExpression*, std::shared_ptr<ConditionExpression>>& cloned) {
    if (!source)
        return {};
    if (const auto found = cloned.find(source.get()); found != cloned.end())
        return found->second;

    auto result = std::make_shared<ConditionExpression>(*source);
    cloned.emplace(source.get(), result);
    result->children.clear();
    for (const auto& child : source->children)
        result->children.push_back(clone_condition(child, cloned));
    return result;
}
} // namespace Didrachma::Strategy::Core::Intern

namespace Didrachma::Strategy::Core {
Snapshot::Snapshot(const Definition& definition) {
    auto copied = std::make_shared<Definition>(definition);
    std::unordered_map<const ConditionExpression*, std::shared_ptr<ConditionExpression>> cloned;
    copied->entry.condition = Intern::clone_condition(definition.entry.condition, cloned);
    for (std::size_t index = 0; index < copied->exits.size(); ++index)
        copied->exits[index].condition = Intern::clone_condition(definition.exits[index].condition, cloned);
    for (std::size_t index = 0; index < copied->runtime_rules.size(); ++index)
        copied->runtime_rules[index].condition =
            Intern::clone_condition(definition.runtime_rules[index].condition, cloned);
    m_definition = std::move(copied);
}
} // namespace Didrachma::Strategy::Core
