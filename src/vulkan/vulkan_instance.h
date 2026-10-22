#pragma once

#include "../util/vk_utils.h"
#include "../window/window.h"

namespace vulkanlearn {

class VulkanInstance {
public:
    VulkanInstance() = default;
    ~VulkanInstance() { reset(); }

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    void initialize(const Window& window) {
        VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        appInfo.pApplicationName = "Vulkan 1.4 Triangle";
        appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        appInfo.pEngineName = "VulkanLearn";
        appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_4;

        const std::vector<const char*> extensions =
            window.requiredVulkanInstanceExtensions();
        if (extensions.empty()) {
            throw std::runtime_error(
                "Window backend did not provide Vulkan instance extensions");
        }

        VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount =
            static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        checkVk(vkCreateInstance(&createInfo, nullptr, &instance_),
                "vkCreateInstance");

        try {
            checkVk(window.createVulkanSurface(instance_, &surface_),
                    "Window::createVulkanSurface");
        } catch (...) {
            reset();
            throw;
        }
    }

    VkInstance handle() const { return instance_; }
    VkSurfaceKHR surface() const { return surface_; }

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    void reset() {
        if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }
};

}  // namespace vulkanlearn
