#include "renderer.h"
#include "window/glfw_window.h"

#include <spdlog/spdlog.h>

#include <exception>

int main() {
    spdlog::info("Starting Vulkan 1.4 triangle demo");
    int exitCode = 0;
    try {
        vulkanlearn::GlfwWindow window(
            vulkanlearn::kWindowWidth, vulkanlearn::kWindowHeight,
            "Vulkan 1.4 - Triangle");
        vulkanlearn::Renderer renderer(window);
        renderer.initialize();

        while (!window.shouldClose()) {
            window.pollEvents();
            renderer.drawFrame();
        }
    } catch (const std::exception& error) {
        spdlog::error("Vulkan demo failed: {}", error.what());
        exitCode = 1;
    }

    return exitCode;
}
