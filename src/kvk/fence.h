#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class Fence {
public:
    explicit Fence(DevicePtr device)
        : Fence(std::move(device), VkFenceCreateInfo{
                                        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}) {}

    Fence(DevicePtr device, VkFenceCreateFlags flags)
        : Fence(std::move(device),
                VkFenceCreateInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr,
                                  flags}) {}

    Fence(DevicePtr device, VkFenceCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("Fence requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        checkVk(vkCreateFence(device_->handle(), &createInfo, nullptr, &fence_),
                "vkCreateFence");
    }

    ~Fence() {
        if (fence_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_->handle(), fence_, nullptr);
        }
    }

    Fence(const Fence&) = delete;
    Fence& operator=(const Fence&) = delete;
    Fence(Fence&&) = delete;
    Fence& operator=(Fence&&) = delete;

    [[nodiscard]] VkFence handle() const noexcept { return fence_; }
    [[nodiscard]] bool valid() const noexcept {
        return fence_ != VK_NULL_HANDLE;
    }

    void wait(uint64_t timeout = UINT64_MAX) const {
        checkVk(vkWaitForFences(device_->handle(), 1, &fence_, VK_TRUE, timeout),
                "vkWaitForFences");
    }

    void reset() const {
        checkVk(vkResetFences(device_->handle(), 1, &fence_), "vkResetFences");
    }

private:
    DevicePtr device_;
    VkFence fence_ = VK_NULL_HANDLE;
};

using FencePtr = std::shared_ptr<Fence>;

}  // namespace klib::vk
