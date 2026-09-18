#pragma once

namespace Didrachma::Apps::Studio {
struct RuntimeOptions {
    bool vsync{true};
    bool discrete_gpu{};
    bool render_diagnostics{};
};

RuntimeOptions parse_runtime_options(int argc, char** argv);
void apply_laptop_gpu_preference(bool discrete_gpu);
} // namespace Didrachma::Apps::Studio
