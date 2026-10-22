#pragma once

#include <kvk/window.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace LearnVulkan {

class GlfwWindow final : public klib::vk::Window {
public:
    GlfwWindow(uint32_t width, uint32_t height, const char* title) {
        if (glfwInit() != GLFW_TRUE) {
            throw std::runtime_error("Failed to initialize GLFW");
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        window_ = glfwCreateWindow(static_cast<int>(width),
                                   static_cast<int>(height), title, nullptr,
                                   nullptr);
        if (window_ == nullptr) {
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window");
        }
        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
    }

    ~GlfwWindow() override {
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
        glfwTerminate();
    }

    GlfwWindow(const GlfwWindow&) = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;

    std::vector<const char*> requiredVulkanInstanceExtensions() const override {
        uint32_t count = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&count);
        if (extensions == nullptr || count == 0) {
            throw std::runtime_error(
                "GLFW could not provide Vulkan instance extensions");
        }
        return {extensions, extensions + count};
    }

    VkResult createVulkanSurface(VkInstance instance,
                                 VkSurfaceKHR* surface) const override {
        return glfwCreateWindowSurface(instance, window_, nullptr, surface);
    }

    VkExtent2D framebufferExtent() const override {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        return {static_cast<uint32_t>(std::max(width, 0)),
                static_cast<uint32_t>(std::max(height, 0))};
    }

    void setResizeCallback(ResizeCallback callback) override {
        resizeCallback_ = std::move(callback);
    }

    void pollEvents() override { glfwPollEvents(); }
    void waitEvents() override { glfwWaitEvents(); }
    bool shouldClose() const override {
        return glfwWindowShouldClose(window_) == GLFW_TRUE;
    }

private:
    GLFWwindow* window_ = nullptr;
    ResizeCallback resizeCallback_;

    static void framebufferSizeCallback(GLFWwindow* window, int, int) {
        auto* self =
            static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window));
        if (self != nullptr && self->resizeCallback_) {
            self->resizeCallback_();
        }
    }
};

}  // namespace klib::vk
