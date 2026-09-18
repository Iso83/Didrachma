#include <Didrachma/stockChart/render/CanvasHost.h>
#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <iostream>

using Didrachma::StockChart::Render::CanvasHost;

int main() {
    if (!glfwInit()) {
        std::cout << "SKIP: GLFW cannot create a hidden context in this environment\n";
        return 77;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    auto* window = glfwCreateWindow(320, 200, "Didrachma render smoke", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        std::cout << "SKIP: OpenGL 3.3 hidden context is unavailable\n";
        return 77;
    }

    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    {
        CanvasHost chart;
        chart.resize({320, 200});
        if (!chart.render_if_needed() || chart.render_if_needed() || chart.render_if_needed())
            return 1;
        if (chart.counters().canvas_renders != 1 || chart.counters().skipped_frames != 2)
            return 1;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
