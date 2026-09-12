#include "Runtime.h"

#include <stdexcept>
#include <string>
#include <ta_libc.h>

namespace Didrachma::Analysis::Adapters::TaLib::Intern {
Runtime::Runtime() {
    if (const auto result = TA_Initialize(); result != TA_SUCCESS)
        throw std::runtime_error("TA-Lib initialization failed with code " + std::to_string(result));
}

Runtime::~Runtime() {
    TA_Shutdown();
}

Runtime& Runtime::instance() {
    static Runtime service;
    return service;
}
} // namespace Didrachma::Analysis::Adapters::TaLib::Intern
