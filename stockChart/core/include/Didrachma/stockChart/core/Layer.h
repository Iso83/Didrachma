#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Didrachma::StockChart::Core {
enum class LayerKind { Line, Band, Histogram, Marker, TimeSpan };
enum class LayerPane { Price, Volume, Separate };

struct Color {
    float red{1.0F}, green{1.0F}, blue{1.0F}, alpha{1.0F};
    friend bool operator==(const Color&, const Color&) = default;
};

struct VisualStyle {
    Color color;
    bool visible{true};
    float line_width{1.0F};
    float band_fill_opacity{0.2F};
    friend bool operator==(const VisualStyle&, const VisualStyle&) = default;
};

struct OutputBinding {
    std::string instance_id;
    std::string output_id;
    friend bool operator==(const OutputBinding&, const OutputBinding&) = default;
};

struct Layer {
    std::string id;
    LayerKind kind{LayerKind::Line};
    std::vector<OutputBinding> outputs;
    VisualStyle style;
    std::uint64_t style_revision{};
    LayerPane pane{LayerPane::Price};
};
} // namespace Didrachma::StockChart::Core
