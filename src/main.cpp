#include "renderer.h"

#include <spdlog/spdlog.h>

#include <exception>

int main() {
    spdlog::info("Starting Vulkan 1.4 triangle demo");
    if (glfwInit() != GLFW_TRUE) {
        spdlog::error("Failed to initialize GLFW");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    GLFWwindow* window =
        glfwCreateWindow(vulkanlearn::kWindowWidth, vulkanlearn::kWindowHeight,
                         "Vulkan 1.4 - Triangle", nullptr, nullptr);
    if (window == nullptr) {
        spdlog::error("Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }

    int exitCode = 0;
    try {
        vulkanlearn::Renderer renderer(window);
        glfwSetWindowUserPointer(window, &renderer);
        glfwSetFramebufferSizeCallback(
            window, vulkanlearn::Renderer::framebufferSizeCallback);
        renderer.initialize();

        while (glfwWindowShouldClose(window) == GLFW_FALSE) {
            glfwPollEvents();
            renderer.drawFrame();
        }
    } catch (const std::exception& error) {
        spdlog::error("Vulkan demo failed: {}", error.what());
        exitCode = 1;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
