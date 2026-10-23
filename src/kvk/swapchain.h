#pragma once

#include <kvk/device.h>
#include <kvk/window.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace klib::vk {

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class Swapchain {
public:
    Swapchain() = default;
    ~Swapchain() { reset(); }

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    void initialize(const Device& device, VkSurfaceKHR surface,
                    Window& window) {
        device_ = device.handle();
        physicalDevice_ = device.physicalDevice();
        deviceGraphicsQueueFamily_ = device.graphicsQueueFamily();
        devicePresentQueueFamily_ = device.presentQueueFamily();
        surface_ = surface;
        window_ = &window;
        create();
    }

    void recreate() {
        destroyResources();
        create();
    }

    VkSwapchainKHR handle() const { return swapchain_; }
    const std::vector<VkImage>& images() const { return images_; }
    const std::vector<VkImageView>& imageViews() const { return imageViews_; }
    VkFormat imageFormat() const { return imageFormat_; }
    VkExtent2D extent() const { return extent_; }
    uint32_t imageCount() const { return static_cast<uint32_t>(images_.size()); }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    Window* window_ = nullptr;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    VkFormat imageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};

    SwapchainSupport querySupport() const {
        SwapchainSupport support;
        checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                    physicalDevice_, surface_, &support.capabilities),
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        uint32_t count = 0;
        checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                    physicalDevice_, surface_, &count, nullptr),
                "vkGetPhysicalDeviceSurfaceFormatsKHR");
        support.formats.resize(count);
        if (count != 0) {
            checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                        physicalDevice_, surface_, &count, support.formats.data()),
                    "vkGetPhysicalDeviceSurfaceFormatsKHR");
        }

        count = 0;
        checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(
                    physicalDevice_, surface_, &count, nullptr),
                "vkGetPhysicalDeviceSurfacePresentModesKHR");
        support.presentModes.resize(count);
        if (count != 0) {
            checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(
                        physicalDevice_, surface_, &count,
                        support.presentModes.data()),
                    "vkGetPhysicalDeviceSurfacePresentModesKHR");
        }
        return support;
    }

    VkSurfaceFormatKHR chooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& formats) const {
        if (formats.empty()) {
            throw std::runtime_error("Surface has no supported formats");
        }
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }
        return formats.front();
    }

    VkPresentModeKHR choosePresentMode(
        const std::vector<VkPresentModeKHR>& modes) const {
        if (modes.empty()) {
            throw std::runtime_error("Surface has no supported present modes");
        }
        // for (VkPresentModeKHR mode : modes) {
        //     if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
        //         return mode;
        //     }
        // }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const {
        if (capabilities.currentExtent.width !=
            std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }

        VkExtent2D selected = window_->framebufferExtent();
        selected.width = std::clamp(selected.width,
                                    capabilities.minImageExtent.width,
                                    capabilities.maxImageExtent.width);
        selected.height = std::clamp(selected.height,
                                     capabilities.minImageExtent.height,
                                     capabilities.maxImageExtent.height);
        return selected;
    }

    VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(
        VkCompositeAlphaFlagsKHR supported) const {
        constexpr VkCompositeAlphaFlagBitsKHR modes[] = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
        for (VkCompositeAlphaFlagBitsKHR mode : modes) {
            if ((supported & mode) != 0) {
                return mode;
            }
        }
        throw std::runtime_error("Surface has no supported composite alpha mode");
    }

    void create() {
        const SwapchainSupport support = querySupport();
        const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
        const VkPresentModeKHR presentMode =
            choosePresentMode(support.presentModes);
        extent_ = chooseExtent(support.capabilities);
        if (extent_.width == 0 || extent_.height == 0) {
            throw std::runtime_error("Cannot create a zero-sized swapchain");
        }
        if ((support.capabilities.supportedUsageFlags &
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
            throw std::runtime_error(
                "Surface images do not support color-attachment usage");
        }

        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 &&
            imageCount > support.capabilities.maxImageCount) {
            imageCount = support.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{
            VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        createInfo.surface = surface_;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent_;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const uint32_t queueFamilyIndices[] = {
            deviceGraphicsQueueFamily_, devicePresentQueueFamily_};
        if (deviceGraphicsQueueFamily_ != devicePresentQueueFamily_) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        createInfo.preTransform = support.capabilities.currentTransform;
        createInfo.compositeAlpha =
            chooseCompositeAlpha(support.capabilities.supportedCompositeAlpha);
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        checkVk(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_),
                "vkCreateSwapchainKHR");
        try {
            checkVk(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount,
                                            nullptr),
                    "vkGetSwapchainImagesKHR");
            images_.resize(imageCount);
            checkVk(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount,
                                            images_.data()),
                    "vkGetSwapchainImagesKHR");
            imageFormat_ = surfaceFormat.format;
            createImageViews();
        } catch (...) {
            destroyResources();
            throw;
        }
    }

    void createImageViews() {
        imageViews_.resize(images_.size(), VK_NULL_HANDLE);
        for (size_t i = 0; i < images_.size(); ++i) {
            VkImageViewCreateInfo createInfo{
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            createInfo.image = images_[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = imageFormat_;
            createInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                                     VK_COMPONENT_SWIZZLE_IDENTITY,
                                     VK_COMPONENT_SWIZZLE_IDENTITY,
                                     VK_COMPONENT_SWIZZLE_IDENTITY};
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.layerCount = 1;
            checkVk(vkCreateImageView(device_, &createInfo, nullptr,
                                      &imageViews_[i]),
                    "vkCreateImageView");
        }
    }

    void destroyResources() {
        for (VkImageView imageView : imageViews_) {
            if (imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(device_, imageView, nullptr);
            }
        }
        imageViews_.clear();
        images_.clear();
        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    void reset() {
        if (device_ != VK_NULL_HANDLE) {
            destroyResources();
        }
    }

    uint32_t deviceGraphicsQueueFamily_ = 0;
    uint32_t devicePresentQueueFamily_ = 0;
};

}  // namespace klib::vk
