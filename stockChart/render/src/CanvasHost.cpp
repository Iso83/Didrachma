#include <Didrachma/stockChart/render/CanvasHost.h>
#include <ScopeCanvas/engine/render/gl/OpenGLApi.h>
#include <chrono>
#include <cmath>

namespace Didrachma::StockChart::Render {
namespace Intern {
void color(float red, float green, float blue, float alpha = 1.0F) {
    glClearColor(red * alpha, green * alpha, blue * alpha, 1.0F);
}

void rectangle(const Rectangle& rectangle, int viewport_height) {
    const auto left = static_cast<int>(rectangle.minimum.x);
    const auto top = static_cast<int>(rectangle.minimum.y);
    const auto width = std::max(1, static_cast<int>(rectangle.maximum.x - rectangle.minimum.x));
    const auto height = std::max(1, static_cast<int>(rectangle.maximum.y - rectangle.minimum.y));
    glScissor(left, viewport_height - top - height, width, height);
    glClear(GL_COLOR_BUFFER_BIT);
}

void segment(Point first, Point second, int viewport_height, int width = 2) {
    const auto steps =
        std::max(1, static_cast<int>(std::max(std::abs(second.x - first.x), std::abs(second.y - first.y))));
    for (int step = 0; step <= steps; ++step) {
        const auto fraction = static_cast<float>(step) / static_cast<float>(steps);
        const Point point{first.x + (second.x - first.x) * fraction, first.y + (second.y - first.y) * fraction};
        rectangle({point, {point.x + static_cast<float>(width), point.y + static_cast<float>(width)}}, viewport_height);
    }
}

void band_segment(Point upper_first, Point upper_second, Point lower_first, Point lower_second, int viewport_height) {
    const auto columns = covering_pixel_columns(upper_first.x, upper_second.x);
    const auto delta = upper_second.x - upper_first.x;
    for (int x = columns.first; x < columns.last_exclusive; ++x) {
        const auto sample_x = static_cast<float>(x) + 0.5F;
        const auto fraction = delta == 0.0F ? 0.0F : std::clamp((sample_x - upper_first.x) / delta, 0.0F, 1.0F);
        const auto upper = upper_first.y + (upper_second.y - upper_first.y) * fraction;
        const auto lower = lower_first.y + (lower_second.y - lower_first.y) * fraction;
        rectangle(
            {{static_cast<float>(x), std::min(upper, lower)}, {static_cast<float>(x + 1), std::max(upper, lower)}},
            viewport_height);
    }
}
} // namespace Intern

class StockChartDrawContext::Resources {
public:
    GLuint vao{}, vbo{};
    Resources() {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
    }
    ~Resources() {
        if (vbo)
            glDeleteBuffers(1, &vbo);
        if (vao)
            glDeleteVertexArrays(1, &vao);
    }
};

StockChartDrawContext::StockChartDrawContext(InvalidationState& invalidation, RenderCounters& counters)
    : m_invalidation(&invalidation), m_counters(&counters) {}

StockChartDrawContext::~StockChartDrawContext() = default;

void StockChartDrawContext::draw(ScopeCanvas::Engine::Render::Window::Viewport*) {
    if (!m_resources)
        m_resources = std::make_unique<Resources>();
    glClearColor(0.055F, 0.065F, 0.085F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    const GLint viewport_height = [] {
        GLint value[4]{};
        glGetIntegerv(GL_VIEWPORT, value);
        return value[3];
    }();

    // Rasterize the band before opaque market layers so it forms one continuous background area.
    for (const auto& band : m_bands) {
        Intern::color(band.color.red, band.color.green, band.color.blue, band.fill_opacity);
        for (std::size_t index = 1; index < band.geometry.upper.size() && index < band.geometry.lower.size(); ++index)
            Intern::band_segment(band.geometry.upper[index - 1], band.geometry.upper[index],
                                 band.geometry.lower[index - 1], band.geometry.lower[index], viewport_height);
    }

    // Draw each linked pane on its own y range while sharing the same x grid.
    Intern::color(0.18F, 0.22F, 0.30F);
    for (int line = 1; line < 5; ++line) {
        const auto content_height = pane_content_height(m_panes.price_content_height);
        const auto y = pane_content_top(m_panes.price_top) + content_height * line / 5;
        glScissor(0, viewport_height - y, m_panes.plot_width, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(m_panes.plot_width * line / 5, 0, 1, viewport_height);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    if (m_panes.separate_height > 0) {
        for (int line = 0; line <= 4; ++line) {
            const auto content_height = pane_content_height(m_panes.separate_height);
            const auto y = pane_content_top(m_panes.separate_top) + content_height * line / 4;
            glScissor(0, viewport_height - y, m_panes.plot_width, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
    glScissor(0, viewport_height - m_panes.volume_top, m_panes.plot_width, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    for (int line = 1; line < 4; ++line) {
        const auto content_height = pane_content_height(m_panes.volume_height);
        const auto y = pane_content_top(m_panes.volume_top) + content_height * line / 4;
        glScissor(0, viewport_height - y, m_panes.plot_width, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    for (const auto& band : m_bands) {
        Intern::color(band.color.red, band.color.green, band.color.blue, band.color.alpha);
        for (std::size_t index = 1; index < band.geometry.upper.size(); ++index) {
            Intern::segment(band.geometry.upper[index - 1], band.geometry.upper[index], viewport_height,
                            static_cast<int>(std::round(band.width)));
            Intern::segment(band.geometry.lower[index - 1], band.geometry.lower[index], viewport_height,
                            static_cast<int>(std::round(band.width)));
        }
    }
    for (std::size_t index = 0; index + 1 < m_candles.wicks.size(); index += 2) {
        const auto& top = m_candles.wicks[index];
        const auto& bottom = m_candles.wicks[index + 1];
        const auto y = viewport_height - static_cast<int>(bottom.y);
        Intern::color(m_candles.bodies[index / 2].rising ? 0.45F : 0.98F,
                      m_candles.bodies[index / 2].rising ? 1.0F : 0.0F,
                      m_candles.bodies[index / 2].rising ? 0.0F : 0.48F);
        glScissor(static_cast<int>(top.x), y, 1, std::max(1, static_cast<int>(bottom.y - top.y)));
        glClear(GL_COLOR_BUFFER_BIT);
    }

    for (const auto& body : m_candles.bodies) {
        Intern::color(body.rising ? 0.45F : 0.98F, body.rising ? 1.0F : 0.0F, body.rising ? 0.0F : 0.48F);
        Intern::rectangle(body, viewport_height);
    }
    for (const auto& bar : m_candles.volume) {
        Intern::color(1.0F, 0.69F, 0.22F);
        Intern::rectangle(bar, viewport_height);
    }
    // Indicator lines are overlays. Drawing them last keeps volume-derived lines such as OBV continuous and readable.
    for (const auto& line : m_lines) {
        Intern::color(line.color.red, line.color.green, line.color.blue, line.color.alpha);
        for (std::size_t index = 1; index < line.geometry.points.size(); ++index)
            Intern::segment(line.geometry.points[index - 1], line.geometry.points[index], viewport_height,
                            static_cast<int>(std::round(line.width)));
    }

    glDisable(GL_SCISSOR_TEST);
    auto elements = m_candles.wicks.size() + m_candles.bodies.size() * 2 + m_candles.volume.size() * 2;
    for (const auto& line : m_lines)
        elements += line.geometry.points.size();
    for (const auto& band : m_bands)
        elements += band.geometry.upper.size() + band.geometry.lower.size();
    m_counters->uploaded_elements += elements;
    m_counters->uploaded_bytes += elements * sizeof(Point);
    ++m_counters->layer_rebuilds;
    ++m_counters->canvas_renders;
    m_invalidation->consume();
}

void CanvasHost::resize(Size size) {
    if (size == m_size)
        return;

    m_size = size;
    m_canvas.setViewportSize(size.width, size.height);
    m_invalidation.invalidate(DirtyReason::Size);
}

bool CanvasHost::render_if_needed() {
    if (!m_context.needsRender()) {
        ++m_counters.skipped_frames;
        return false;
    }

    const auto begin = std::chrono::steady_clock::now();
    m_canvas.draw();
    m_counters.last_render_time = std::chrono::steady_clock::now() - begin;
    m_counters.render_time += m_counters.last_render_time;
    return true;
}
} // namespace Didrachma::StockChart::Render
