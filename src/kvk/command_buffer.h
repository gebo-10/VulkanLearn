#pragma once

#include <kvk/command_pool.h>

#include <memory>

namespace klib::vk {

class CommandBuffer {
public:
    CommandBuffer(CommandPoolPtr pool,
                  VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY)
        : pool_(std::move(pool)) {
        if (!pool_) {
            throw std::invalid_argument(
                "CommandBuffer requires a valid CommandPool");
        }
        const auto buffers = pool_->allocate(1, level);
        commandBuffer_ = buffers.front();
    }

    ~CommandBuffer() {
        if (commandBuffer_ != VK_NULL_HANDLE && pool_) {
            vkFreeCommandBuffers(pool_->deviceHandle(), pool_->handle(), 1,
                                 &commandBuffer_);
        }
    }

    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;
    CommandBuffer(CommandBuffer&&) = delete;
    CommandBuffer& operator=(CommandBuffer&&) = delete;

    [[nodiscard]] VkCommandBuffer handle() const noexcept {
        return commandBuffer_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return commandBuffer_ != VK_NULL_HANDLE;
    }

    void begin(const VkCommandBufferBeginInfo& beginInfo) const {
        checkVk(vkBeginCommandBuffer(commandBuffer_, &beginInfo),
                "vkBeginCommandBuffer");
    }

    void end() const {
        checkVk(vkEndCommandBuffer(commandBuffer_), "vkEndCommandBuffer");
    }

    void reset(VkCommandBufferResetFlags flags = 0) const {
        checkVk(vkResetCommandBuffer(commandBuffer_, flags),
                "vkResetCommandBuffer");
    }

private:
    CommandPoolPtr pool_;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
};

using CommandBufferPtr = std::shared_ptr<CommandBuffer>;

}  // namespace klib::vk
