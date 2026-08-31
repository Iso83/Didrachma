#pragma once

#include <GLFW/glfw3.h>
#include <glad/glad.h>
#include <imgui.h>
#include <implot.h>
#include <string>

namespace Didrachma::Apps::Chart {
struct App {
    ImVec4 ClearColor;
    GLFWwindow* Window{};
    bool UsingDGPU{}; // using discrete gpu (laptops only)

    App(std::string title, int w, int h, int argc, char const* argv[]);
    virtual ~App();

    virtual void Start() {}
    virtual void Update() = 0;
    void Run();

    ImVec2 GetWindowSize() const {
        int w, h;
        glfwGetWindowSize(Window, &w, &h);
        return ImVec2(w, h);
    }
};
} // namespace Didrachma::Apps::Chart
