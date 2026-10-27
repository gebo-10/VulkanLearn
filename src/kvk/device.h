#pragma once

#include <kvk/instance.h>
#include <kvk/surface.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace klib::vk {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;

    bool complete(bool requirePresent = true) const {
        return graphics.has_value() && (!requirePresent || present.has_value());
    }
};

class Device {
public:
    explicit Device(InstancePtr instance, SurfacePtr surface)
        : instanceOwner_(std::move(instance)), surface_(std::move(surface)) {
        if (!instanceOwner_) {
            throw std::invalid_argument("Device requires a valid Instance");
        }
        instance_ = instanceOwner_->handle();
        const bool requiresPresentation = surface_ != nullptr;

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

            if (families.complete(requiresPresentation) &&
                (!requiresPresentation ||
                 (supportsSwapchain(candidate) && supportsSurface(candidate))) &&
                vulkan13Features.dynamicRendering == VK_TRUE) {
                physicalDevice_ = candidate;
                queueFamilies_ = families;
                spdlog::info("Using Vulkan 1.4 device: {}", properties.deviceName);
                createLogicalDevice();
                return;
            }
        }

        throw std::runtime_error(requiresPresentation
                                     ? "No GPU supports Vulkan 1.4, dynamic "
                                       "rendering, and swapchain presentation"
                                     : "No GPU supports Vulkan 1.4 and dynamic "
                                       "rendering");
    }

    ~Device() { reset(); }

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice handle() const { return device_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    VkQueue presentQueue() const { return presentQueue_; }
    uint32_t graphicsQueueFamily() const { return *queueFamilies_.graphics; }
    uint32_t presentQueueFamily() const {
        if (!queueFamilies_.present) {
            throw std::logic_error(
                "Device was created without a presentation queue");
        }
        return *queueFamilies_.present;
    }
    const QueueFamilyIndices& queueFamilies() const { return queueFamilies_; }

    void waitIdle() const { checkVk(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle"); }

    uint32_t findMemoryType(uint32_t typeFilter,
                            VkMemoryPropertyFlags requiredProperties) const {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) != 0 &&
                (memoryProperties.memoryTypes[i].propertyFlags &
                 requiredProperties) == requiredProperties) {
                return i;
            }
        }
        throw std::runtime_error("No compatible Vulkan memory type was found");
    }

private:
    InstancePtr instanceOwner_;
    VkInstance instance_ = VK_NULL_HANDLE;
    SurfacePtr surface_ = nullptr;
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
            if (surface_ != nullptr) {
                VkBool32 supportsPresent = VK_FALSE;
                checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(
                            device, i, surface_->handle(), &supportsPresent),
                        "vkGetPhysicalDeviceSurfaceSupportKHR");
                if (supportsPresent == VK_TRUE) {
                    families.present = i;
                }
            }
            if (families.complete(surface_ != nullptr)) {
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

    bool supportsSurface(VkPhysicalDevice device) const {
        uint32_t formatCount = 0;
        checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                    device, surface_->handle(), &formatCount, nullptr),
                "vkGetPhysicalDeviceSurfaceFormatsKHR");
        uint32_t presentModeCount = 0;
        checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(
                    device, surface_->handle(), &presentModeCount, nullptr),
                "vkGetPhysicalDeviceSurfacePresentModesKHR");
        return formatCount > 0 && presentModeCount > 0;
    }

    void createLogicalDevice() {
        std::set<uint32_t> uniqueFamilies = {*queueFamilies_.graphics};
        if (queueFamilies_.present) {
            uniqueFamilies.insert(*queueFamilies_.present);
        }
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

        const char* swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        createInfo.pNext = &vulkan13Features;
        createInfo.queueCreateInfoCount =
            static_cast<uint32_t>(queueInfos.size());
        createInfo.pQueueCreateInfos = queueInfos.data();
        createInfo.pEnabledFeatures = &baseFeatures;
        createInfo.enabledExtensionCount =
            surface_ != nullptr ? 1u : 0u;
        createInfo.ppEnabledExtensionNames =
            surface_ != nullptr ? &swapchainExtension : nullptr;
        checkVk(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
                "vkCreateDevice");
        vkGetDeviceQueue(device_, *queueFamilies_.graphics, 0, &graphicsQueue_);
        if (queueFamilies_.present) {
            vkGetDeviceQueue(device_, *queueFamilies_.present, 0,
                             &presentQueue_);
        }
    }

    void reset() {
        if (device_ != VK_NULL_HANDLE) {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
    }
};
typedef std::shared_ptr<Device> DevicePtr;
}  // namespace klib::vk
