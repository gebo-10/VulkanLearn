#pragma once

#include <kvk/device.h>
#include <kvk/image_view.h>
#include <kvk/window.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

namespace klib::vk {

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class Swapchain {
public:
    Swapchain(DevicePtr device, VkSurfaceKHR surface, Window& window)
        : device_(device ? device->handle() : VK_NULL_HANDLE),
          physicalDevice_(device ? device->physicalDevice() : VK_NULL_HANDLE),
          surface_(surface),
          window_(&window),
          deviceGraphicsQueueFamily_(device ? device->graphicsQueueFamily() : 0),
          devicePresentQueueFamily_(device ? device->presentQueueFamily() : 0) {
        if (!device) {
            throw std::invalid_argument("Swapchain requires a valid Device");
        }
        create(device);
    }

    ~Swapchain() { reset(); }

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    void recreate(DevicePtr device) {
        if (!device || device->handle() != device_) {
            throw std::invalid_argument(
                "Swapchain recreation requires its original Device");
        }
        destroyResources();
        create(device);
    }

    VkSwapchainKHR handle() const { return swapchain_; }
    VkResult acquireNextImage(uint64_t timeout, VkSemaphore semaphore,
                              VkFence fence, uint32_t& imageIndex) const {
        return vkAcquireNextImageKHR(device_, swapchain_, timeout, semaphore,
                                     fence, &imageIndex);
    }
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
    std::vector<ImageViewPtr> imageViewObjects_;
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

    void create(const DevicePtr& device) {
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

        try {
            createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            checkVk(vkCreateSwapchainKHR(device_, &createInfo, nullptr,
                                         &swapchain_),
                    "vkCreateSwapchainKHR");

            uint32_t actualImageCount = 0;
            checkVk(vkGetSwapchainImagesKHR(device_, swapchain_,
                                            &actualImageCount, nullptr),
                    "vkGetSwapchainImagesKHR");
            images_.resize(actualImageCount);
            if (actualImageCount != 0) {
                checkVk(vkGetSwapchainImagesKHR(device_, swapchain_,
                                                &actualImageCount,
                                                images_.data()),
                        "vkGetSwapchainImagesKHR");
                images_.resize(actualImageCount);
            }
            imageFormat_ = surfaceFormat.format;
            createImageViews(device);
        } catch (...) {
            destroyResources();
            throw;
        }
    }

    void createImageViews(const DevicePtr& device) {
        imageViews_.resize(images_.size(), VK_NULL_HANDLE);
        imageViewObjects_.resize(images_.size());
        for (size_t i = 0; i < images_.size(); ++i) {
            imageViewObjects_[i] = std::make_shared<ImageView>(
                device, images_[i], imageFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
            imageViews_[i] = imageViewObjects_[i]->handle();
        }
    }

    void destroyResources() {
        imageViewObjects_.clear();
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

using SwapchainPtr = std::shared_ptr<Swapchain>;

}  // namespace klib::vk
