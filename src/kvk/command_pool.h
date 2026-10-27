#pragma once

#include <kvk/device.h>

#include <memory>
#include <vector>

namespace klib::vk {

class CommandPool {
public:
    CommandPool(DevicePtr device, uint32_t queueFamilyIndex,
                VkCommandPoolCreateFlags flags =
                    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
        : CommandPool(std::move(device),
                      makeCreateInfo(queueFamilyIndex, flags)) {}

    CommandPool(DevicePtr device, VkCommandPoolCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("CommandPool requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        checkVk(vkCreateCommandPool(device_->handle(), &createInfo, nullptr,
                                    &pool_),
                "vkCreateCommandPool");
    }

    ~CommandPool() {
        if (pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_->handle(), pool_, nullptr);
        }
    }

    CommandPool(const CommandPool&) = delete;
    CommandPool& operator=(const CommandPool&) = delete;
    CommandPool(CommandPool&&) = delete;
    CommandPool& operator=(CommandPool&&) = delete;

    [[nodiscard]] VkCommandPool handle() const noexcept { return pool_; }
    [[nodiscard]] VkDevice deviceHandle() const noexcept {
        return device_->handle();
    }
    [[nodiscard]] DevicePtr device() const noexcept { return device_; }
    [[nodiscard]] bool valid() const noexcept {
        return pool_ != VK_NULL_HANDLE;
    }

    std::vector<VkCommandBuffer> allocate(
        uint32_t count,
        VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) const {
        std::vector<VkCommandBuffer> commandBuffers(count, VK_NULL_HANDLE);
        if (count == 0) {
            return commandBuffers;
        }
        VkCommandBufferAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandPool = pool_;
        allocateInfo.level = level;
        allocateInfo.commandBufferCount = count;
        checkVk(vkAllocateCommandBuffers(device_->handle(), &allocateInfo,
                                         commandBuffers.data()),
                "vkAllocateCommandBuffers");
        return commandBuffers;
    }

    void reset(VkCommandPoolResetFlags flags = 0) const {
        checkVk(vkResetCommandPool(device_->handle(), pool_, flags),
                "vkResetCommandPool");
    }

private:
    DevicePtr device_;
    VkCommandPool pool_ = VK_NULL_HANDLE;

    static VkCommandPoolCreateInfo makeCreateInfo(
        uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags) {
        VkCommandPoolCreateInfo createInfo{};
        createInfo.flags = flags;
        createInfo.queueFamilyIndex = queueFamilyIndex;
        return createInfo;
    }
};

using CommandPoolPtr = std::shared_ptr<CommandPool>;

}  // namespace klib::vk
