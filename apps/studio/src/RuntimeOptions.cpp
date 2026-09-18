#include "RuntimeOptions.h"

#include <string_view>

#if defined(_WIN32)
extern "C" __declspec(dllexport) unsigned long NvOptimusEnablement = 0;
extern "C" __declspec(dllexport) unsigned long AmdPowerXpressRequestHighPerformance = 0;
#endif

namespace Didrachma::Apps::Studio {
RuntimeOptions parse_runtime_options(int argc, char** argv) {
    RuntimeOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--no-vsync")
            options.vsync = false;
        else if (argument == "--gpu")
            options.discrete_gpu = true;
        else if (argument == "--render-info")
            options.render_diagnostics = true;
    }
    return options;
}

void apply_laptop_gpu_preference(bool discrete_gpu) {
#if defined(_WIN32)
    NvOptimusEnablement = AmdPowerXpressRequestHighPerformance = discrete_gpu ? 1UL : 0UL;
#else
    (void)discrete_gpu;
#endif
}
} // namespace Didrachma::Apps::Studio
