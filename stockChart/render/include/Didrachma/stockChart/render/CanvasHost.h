#pragma once

#include <Didrachma/stockChart/render/State.h>
#include <ScopeCanvas/engine/render/window/Canvas.h>
#include <ScopeCanvas/engine/render/window/DrawContext.h>
#include <memory>

namespace Didrachma::StockChart::Render {
class StockChartDrawContext final : public ScopeCanvas::Engine::Render::Window::DrawContext {
    class Resources;
    std::unique_ptr<Resources> m_resources;
    CandleGeometry m_candles;
    std::vector<StyledLineGeometry> m_lines;
    std::vector<StyledBandGeometry> m_bands;
    PaneLayout m_panes{};
    InvalidationState* m_invalidation{};
    RenderCounters* m_counters{};

public:
    StockChartDrawContext(InvalidationState& invalidation, RenderCounters& counters);
    ~StockChartDrawContext() override;
    StockChartDrawContext(const StockChartDrawContext&) = delete;

    StockChartDrawContext& operator=(const StockChartDrawContext&) = delete;

    void set_geometry(CandleGeometry geometry) {
        m_candles = std::move(geometry);
        m_invalidation->invalidate(DirtyReason::Data);
    }
    void set_indicators(std::vector<StyledLineGeometry> lines, std::vector<StyledBandGeometry> bands) {
        m_lines = std::move(lines);
        m_bands = std::move(bands);
        m_invalidation->invalidate(DirtyReason::Data);
    }
    void set_pane_layout(PaneLayout panes) {
        m_panes = panes;
        m_invalidation->invalidate(DirtyReason::Size);
    }
    void draw(ScopeCanvas::Engine::Render::Window::Viewport* view) override;
    bool needsRender() override {
        return m_invalidation->needs_render();
    }
};

class CanvasHost {
    InvalidationState m_invalidation;
    RenderCounters m_counters;
    StockChartDrawContext m_context;
    ScopeCanvas::Engine::Render::Window::Canvas m_canvas;
    Size m_size{};

public:
    CanvasHost() : m_context(m_invalidation, m_counters) {
        m_canvas.registerDrawContext(&m_context);
    }

    [[nodiscard]] unsigned int texture() const {
        return m_canvas.colorTexture();
    }
    [[nodiscard]] const RenderCounters& counters() const {
        return m_counters;
    }
    [[nodiscard]] InvalidationState& invalidation() {
        return m_invalidation;
    }
    [[nodiscard]] ScopeCanvas::Engine::Render::Window::Canvas& canvas() {
        return m_canvas;
    }

    void set_bar_counts(std::size_t viewport, std::size_t loaded) {
        m_counters.viewport_bars = viewport;
        m_counters.loaded_bars = loaded;
    }

    void set_geometry(CandleGeometry geometry) {
        m_context.set_geometry(std::move(geometry));
    }
    void set_indicators(std::vector<StyledLineGeometry> lines, std::vector<StyledBandGeometry> bands) {
        m_context.set_indicators(std::move(lines), std::move(bands));
    }
    void set_pane_layout(PaneLayout panes) {
        m_context.set_pane_layout(panes);
    }
    void resize(Size size);
    bool render_if_needed();
};
} // namespace Didrachma::StockChart::Render
