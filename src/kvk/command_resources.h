#pragma once

#include <kvk/command_buffer.h>
#include <kvk/command_pool.h>

#include <vector>

namespace klib::vk {

class CommandResources {
public:
    CommandResources() = default;
    ~CommandResources() = default;

    CommandResources(const CommandResources&) = delete;
    CommandResources& operator=(const CommandResources&) = delete;

    void initialize(DevicePtr device, uint32_t commandBufferCount) {
        if (!device) {
            throw std::invalid_argument(
                "CommandResources requires a valid Device");
        }
        const uint32_t queueFamilyIndex = device->graphicsQueueFamily();
        pool_ =
            std::make_shared<CommandPool>(std::move(device), queueFamilyIndex);
        allocate(commandBufferCount);
    }

    void recreate(uint32_t commandBufferCount) {
        commandBuffers_.clear();
        allocate(commandBufferCount);
    }

    VkCommandBuffer handle(uint32_t index) const {
        return commandBuffers_.at(index)->handle();
    }

private:
    CommandPoolPtr pool_;
    std::vector<CommandBufferPtr> commandBuffers_;

    void allocate(uint32_t count) {
        commandBuffers_.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            commandBuffers_.push_back(std::make_shared<CommandBuffer>(pool_));
        }
    }
};

}  // namespace klib::vk
