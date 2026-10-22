#pragma once

#include <kvk/device.h>

#include <vector>

namespace klib::vk {

class CommandResources {
public:
    CommandResources() = default;
    ~CommandResources() { reset(); }

    CommandResources(const CommandResources&) = delete;
    CommandResources& operator=(const CommandResources&) = delete;

    void initialize(const Device& device, uint32_t commandBufferCount) {
        device_ = device.handle();
        VkCommandPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = device.graphicsQueueFamily();
        checkVk(vkCreateCommandPool(device_, &poolInfo, nullptr, &pool_),
                "vkCreateCommandPool");
        allocate(commandBufferCount);
    }

    void recreate(uint32_t commandBufferCount) {
        if (!commandBuffers_.empty()) {
            vkFreeCommandBuffers(device_, pool_,
                                 static_cast<uint32_t>(commandBuffers_.size()),
                                 commandBuffers_.data());
            commandBuffers_.clear();
        }
        allocate(commandBufferCount);
    }

    VkCommandBuffer handle(uint32_t index) const {
        return commandBuffers_.at(index);
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    void allocate(uint32_t count) {
        commandBuffers_.resize(count, VK_NULL_HANDLE);
        VkCommandBufferAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandPool = pool_;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = count;
        if (count != 0) {
            checkVk(vkAllocateCommandBuffers(device_, &allocateInfo,
                                             commandBuffers_.data()),
                    "vkAllocateCommandBuffers");
        }
    }

    void reset() {
        if (device_ != VK_NULL_HANDLE && pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, pool_, nullptr);
            pool_ = VK_NULL_HANDLE;
            commandBuffers_.clear();
        }
    }
};

}  // namespace klib::vk
