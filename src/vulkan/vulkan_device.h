#pragma once

#include "vulkan_instance.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace vulkanlearn {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;

    bool complete() const { return graphics.has_value() && present.has_value(); }
};

class VulkanDevice {
public:
    VulkanDevice() = default;
    ~VulkanDevice() { reset(); }

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    void initialize(const VulkanInstance& instance) {
        instance_ = instance.handle();
        surface_ = instance.surface();

        uint32_t count = 0;
        checkVk(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
                "vkEnumeratePhysicalDevices");
        if (count == 0) {
            throw std::runtime_error("No Vulkan-capable GPU was found");
        }

        std::vector<VkPhysicalDevice> devices(count);
        checkVk(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
                "vkEnumeratePhysicalDevices");

        for (VkPhysicalDevice candidate : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_4) {
                continue;
            }

            const QueueFamilyIndices families = findQueueFamilies(candidate);
            VkPhysicalDeviceVulkan14Features vulkan14Features{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
            VkPhysicalDeviceVulkan13Features vulkan13Features{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            vulkan13Features.pNext = &vulkan14Features;
            VkPhysicalDeviceFeatures2 features{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &vulkan13Features;
            vkGetPhysicalDeviceFeatures2(candidate, &features);

            if (families.complete() && supportsSwapchain(candidate) &&
                vulkan13Features.dynamicRendering == VK_TRUE) {
                physicalDevice_ = candidate;
                queueFamilies_ = families;
                spdlog::info("Using Vulkan 1.4 device: {}", properties.deviceName);
                createLogicalDevice();
                return;
            }
        }

        throw std::runtime_error(
            "No GPU supports Vulkan 1.4, dynamic rendering, and swapchain "
            "presentation");
    }

    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice handle() const { return device_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    VkQueue presentQueue() const { return presentQueue_; }
    uint32_t graphicsQueueFamily() const { return *queueFamilies_.graphics; }
    uint32_t presentQueueFamily() const { return *queueFamilies_.present; }
    const QueueFamilyIndices& queueFamilies() const { return queueFamilies_; }

    void waitIdle() const { checkVk(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle"); }

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    QueueFamilyIndices queueFamilies_;

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const {
        QueueFamilyIndices families;
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> properties(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());

        for (uint32_t i = 0; i < count; ++i) {
            if ((properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                families.graphics = i;
            }
            VkBool32 supportsPresent = VK_FALSE;
            checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(
                        device, i, surface_, &supportsPresent),
                    "vkGetPhysicalDeviceSurfaceSupportKHR");
            if (supportsPresent == VK_TRUE) {
                families.present = i;
            }
            if (families.complete()) {
                break;
            }
        }
        return families;
    }

    bool supportsSwapchain(VkPhysicalDevice device) const {
        uint32_t count = 0;
        checkVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count,
                                                      nullptr),
                "vkEnumerateDeviceExtensionProperties");
        std::vector<VkExtensionProperties> extensions(count);
        checkVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count,
                                                      extensions.data()),
                "vkEnumerateDeviceExtensionProperties");
        return std::any_of(extensions.begin(), extensions.end(),
                           [](const VkExtensionProperties& extension) {
                               return std::string(extension.extensionName) ==
                                      VK_KHR_SWAPCHAIN_EXTENSION_NAME;
                           });
    }

    void createLogicalDevice() {
        const std::set<uint32_t> uniqueFamilies = {
            *queueFamilies_.graphics, *queueFamilies_.present};
        const float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        for (uint32_t family : uniqueFamilies) {
            VkDeviceQueueCreateInfo queueInfo{
                VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &priority;
            queueInfos.push_back(queueInfo);
        }

        VkPhysicalDeviceFeatures baseFeatures{};
        VkPhysicalDeviceVulkan14Features vulkan14Features{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
        VkPhysicalDeviceVulkan13Features vulkan13Features{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        vulkan13Features.dynamicRendering = VK_TRUE;
        vulkan13Features.pNext = &vulkan14Features;

        const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        createInfo.pNext = &vulkan13Features;
        createInfo.queueCreateInfoCount =
            static_cast<uint32_t>(queueInfos.size());
        createInfo.pQueueCreateInfos = queueInfos.data();
        createInfo.pEnabledFeatures = &baseFeatures;
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = extensions;
        checkVk(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
                "vkCreateDevice");
        vkGetDeviceQueue(device_, *queueFamilies_.graphics, 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, *queueFamilies_.present, 0, &presentQueue_);
    }

    void reset() {
        if (device_ != VK_NULL_HANDLE) {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
    }
};

}  // namespace vulkanlearn
