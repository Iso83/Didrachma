#pragma once

namespace Didrachma::Analysis::Adapters::TaLib::Intern {
class Runtime {
    Runtime();
    ~Runtime();

public:
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    static Runtime& instance();
};
} // namespace Didrachma::Analysis::Adapters::TaLib::Intern
