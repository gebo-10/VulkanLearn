#include <kvk/renderer.h>
#include "glfw_window.h"

#include <spdlog/spdlog.h>

#include <exception>

int main() {
    spdlog::info("Starting Vulkan 1.4 triangle demo");
    int exitCode = 0;
    try {
        LearnVulkan::GlfwWindow window(
            klib::vk::kWindowWidth, klib::vk::kWindowHeight,
            "Vulkan 1.4 - Triangle");
        klib::vk::Renderer renderer(window);
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
