#pragma once

#include <Didrachma/analysis/core/indicator/Definition.h>
#include <string>
#include <string_view>
#include <ta_abstract.h>
#include <vector>

namespace Didrachma::Analysis::Adapters::TaLib::Intern {
struct Option {
    std::string id;
    bool integer{};
};

struct Input {
    enum class Kind { Price, Real } kind{};
    int flags{};
};

struct Output {
    std::string id;
    bool integer{};
};

struct Descriptor {
    const TA_FuncHandle* handle{};
    std::string ta_name;
    Core::Indicator::Definition definition;
    std::vector<Input> inputs;
    std::vector<Option> options;
    std::vector<Output> outputs;
    int flags{};
};

const std::vector<Descriptor>& descriptors();
std::vector<Core::Indicator::Definition> catalog();
const Descriptor* descriptor(std::string_view id);
} // namespace Didrachma::Analysis::Adapters::TaLib::Intern
