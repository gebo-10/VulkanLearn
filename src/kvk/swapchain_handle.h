#pragma once

#include <kvk/device.h>

#include <memory>
#include <vector>

namespace klib::vk {

class SwapchainHandle {
public:
    SwapchainHandle(VkDevice device,
                    VkSwapchainCreateInfoKHR createInfo)
        : device_(device) {
        if (device_ == VK_NULL_HANDLE) {
            throw std::invalid_argument("SwapchainHandle requires a valid device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        checkVk(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_),
                "vkCreateSwapchainKHR");
    }

    ~SwapchainHandle() {
        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        }
    }

    SwapchainHandle(const SwapchainHandle&) = delete;
    SwapchainHandle& operator=(const SwapchainHandle&) = delete;
    SwapchainHandle(SwapchainHandle&&) = delete;
    SwapchainHandle& operator=(SwapchainHandle&&) = delete;

    [[nodiscard]] VkSwapchainKHR handle() const noexcept { return swapchain_; }
    [[nodiscard]] bool valid() const noexcept {
        return swapchain_ != VK_NULL_HANDLE;
    }

    std::vector<VkImage> images() const {
        uint32_t count = 0;
        checkVk(vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr),
                "vkGetSwapchainImagesKHR");
        std::vector<VkImage> swapchainImages(count);
        if (count != 0) {
            checkVk(vkGetSwapchainImagesKHR(device_, swapchain_, &count,
                                            swapchainImages.data()),
                    "vkGetSwapchainImagesKHR");
            swapchainImages.resize(count);
        }
        return swapchainImages;
    }

    VkResult acquireNextImage(uint64_t timeout, VkSemaphore semaphore,
                              VkFence fence, uint32_t& imageIndex) const {
        return vkAcquireNextImageKHR(device_, swapchain_, timeout, semaphore,
                                     fence, &imageIndex);
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
};

using SwapchainHandlePtr = std::shared_ptr<SwapchainHandle>;

}  // namespace klib::vk
