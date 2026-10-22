#pragma once

#include <vulkan/vulkan.h>

#include <functional>
#include <vector>

namespace klib::vk {

class Window {
public:
    using ResizeCallback = std::function<void()>;

    virtual ~Window() = default;

    virtual std::vector<const char*> requiredVulkanInstanceExtensions() const = 0;
    virtual VkResult createVulkanSurface(VkInstance instance,
                                         VkSurfaceKHR* surface) const = 0;
    virtual VkExtent2D framebufferExtent() const = 0;
    virtual void setResizeCallback(ResizeCallback callback) = 0;
    virtual void pollEvents() = 0;
    virtual void waitEvents() = 0;
    virtual bool shouldClose() const = 0;
};

}  // namespace klib::vk
