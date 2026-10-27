#pragma once
#include <vulkan/vulkan.h>
#include <kvk/fence.h>
#include <kvk/renderer_config.h>
#include <kvk/semaphore.h>

#include <array>
#include <memory>

namespace klib::vk {

class SyncObjects {
public:
    explicit SyncObjects(DevicePtr device) {
        if (!device) {
            throw std::invalid_argument("SyncObjects requires a valid Device");
        }
        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            frames_[i].imageAvailable = std::make_shared<Semaphore>(device);
            frames_[i].renderFinished = std::make_shared<Semaphore>(device);
            frames_[i].fence =
                std::make_shared<Fence>(device, VK_FENCE_CREATE_SIGNALED_BIT);
        }
    }

    ~SyncObjects() = default;

    SyncObjects(const SyncObjects&) = delete;
    SyncObjects& operator=(const SyncObjects&) = delete;

    struct FrameSync {
        SemaphorePtr imageAvailable;
        SemaphorePtr renderFinished;
        FencePtr fence;
    };

    VkSemaphore imageAvailable(uint32_t frame) const {
        return frames_.at(frame).imageAvailable->handle();
    }
    VkSemaphore renderFinished(uint32_t frame) const {
        return frames_.at(frame).renderFinished->handle();
    }
    VkFence fence(uint32_t frame) const {
        return frames_.at(frame).fence->handle();
    }
    void waitForFrame(uint32_t frame, uint64_t timeout = UINT64_MAX) const {
        frames_.at(frame).fence->wait(timeout);
    }
    void resetFrameFence(uint32_t frame) const {
        frames_.at(frame).fence->reset();
    }

private:
    std::array<FrameSync, kFramesInFlight> frames_{};
};

using SyncObjectsPtr = std::shared_ptr<SyncObjects>;

}  // namespace klib::vk
