#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan.h>
#include <vector>

namespace klib::vk {

inline void checkVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(result) + ")");
    }
}

class Instance {
public:
    explicit Instance(const std::vector<const char*>& extensions = {}) {
        VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        appInfo.pApplicationName = "Vulkan 1.4 Triangle";
        appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        appInfo.pEngineName = "VulkanLearn";
        appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_4;

        VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount =
            static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        checkVk(vkCreateInstance(&createInfo, nullptr, &instance_),
                "vkCreateInstance");
    }

    ~Instance() { reset(); }

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    VkInstance handle() const { return instance_; }

private:
    VkInstance instance_ = VK_NULL_HANDLE;

    void reset() {
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }
};

using InstancePtr = std::shared_ptr<Instance>;

}  // namespace klib::vk
