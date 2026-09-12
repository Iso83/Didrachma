#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <implot.h>
#include <string>

namespace Didrachma::Apps::Chart {
class App {
protected:
    ImVec4 ClearColor;
    GLFWwindow* Window{};
    bool UsingDGPU{}; // using discrete gpu (laptops only)

    virtual void Start() {}
    virtual void Update() = 0;
    ImVec2 GetWindowSize() const;

public:
    App(std::string title, int w, int h, int argc, char const* argv[]);
    virtual ~App();

    void Run();
};
} // namespace Didrachma::Apps::Chart
