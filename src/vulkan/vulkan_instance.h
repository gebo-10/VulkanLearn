#pragma once

#include "../util/vk_utils.h"

namespace vulkanlearn {

class VulkanInstance {
public:
    VulkanInstance() = default;
    ~VulkanInstance() { reset(); }

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    void initialize(GLFWwindow* window) {
        VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        appInfo.pApplicationName = "Vulkan 1.4 Triangle";
        appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        appInfo.pEngineName = "VulkanLearn";
        appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_4;

        uint32_t extensionCount = 0;
        const char** extensions =
            glfwGetRequiredInstanceExtensions(&extensionCount);
        if (extensions == nullptr || extensionCount == 0) {
            throw std::runtime_error(
                "GLFW could not provide Vulkan instance extensions");
        }

        VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = extensionCount;
        createInfo.ppEnabledExtensionNames = extensions;
        checkVk(vkCreateInstance(&createInfo, nullptr, &instance_),
                "vkCreateInstance");

        try {
            checkVk(glfwCreateWindowSurface(instance_, window, nullptr, &surface_),
                    "glfwCreateWindowSurface");
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
