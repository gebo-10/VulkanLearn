#pragma once
#include <vulkan/vulkan.h>
#include <kvk/utils.h>
#include <kvk/renderer_config.h>

#include <array>

namespace klib::vk {

class SyncObjects {
public:
    SyncObjects() = default;
    ~SyncObjects() { reset(); }

    SyncObjects(const SyncObjects&) = delete;
    SyncObjects& operator=(const SyncObjects&) = delete;

    struct FrameSync {
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };

    void initialize(VkDevice device) {
        device_ = device;
        VkSemaphoreCreateInfo semaphoreInfo{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            checkVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                                      &frames_[i].imageAvailable),
                    "vkCreateSemaphore");
            checkVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                                      &frames_[i].renderFinished),
                    "vkCreateSemaphore");
            checkVk(vkCreateFence(device_, &fenceInfo, nullptr, &frames_[i].fence),
                    "vkCreateFence");
        }
    }

    VkSemaphore imageAvailable(uint32_t frame) const {
        return frames_.at(frame).imageAvailable;
    }
    VkSemaphore renderFinished(uint32_t frame) const {
        return frames_.at(frame).renderFinished;
    }
    VkFence fence(uint32_t frame) const { return frames_.at(frame).fence; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    std::array<FrameSync, kFramesInFlight> frames_{};

    void reset() {
        if (device_ == VK_NULL_HANDLE) {
            return;
        }
        for (uint32_t i = kFramesInFlight; i > 0; --i) {
            FrameSync& frame = frames_[i - 1];
            if (frame.fence != VK_NULL_HANDLE) {
                vkDestroyFence(device_, frame.fence, nullptr);
                frame.fence = VK_NULL_HANDLE;
            }
            if (frame.renderFinished != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, frame.renderFinished, nullptr);
                frame.renderFinished = VK_NULL_HANDLE;
            }
            if (frame.imageAvailable != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, frame.imageAvailable, nullptr);
                frame.imageAvailable = VK_NULL_HANDLE;
            }
        }
        device_ = VK_NULL_HANDLE;
    }
};

}  // namespace klib::vk
