#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class Buffer {
public:
    Buffer(DevicePtr device, VkDeviceSize size, VkBufferUsageFlags usage,
           VkBufferCreateFlags flags = 0)
        : Buffer(std::move(device), makeCreateInfo(size, usage, flags)) {}

    Buffer(DevicePtr device, VkBufferCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("Buffer requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        checkVk(vkCreateBuffer(device_->handle(), &createInfo, nullptr, &buffer_),
                "vkCreateBuffer");
    }

    ~Buffer() {
        if (buffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_->handle(), buffer_, nullptr);
        }
    }

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(Buffer&&) = delete;

    [[nodiscard]] VkBuffer handle() const noexcept { return buffer_; }
    [[nodiscard]] bool valid() const noexcept {
        return buffer_ != VK_NULL_HANDLE;
    }

    VkMemoryRequirements memoryRequirements() const {
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_->handle(), buffer_,
                                      &requirements);
        return requirements;
    }

    void bindMemory(VkDeviceMemory memory, VkDeviceSize offset = 0) const {
        checkVk(vkBindBufferMemory(device_->handle(), buffer_, memory, offset),
                "vkBindBufferMemory");
    }

private:
    DevicePtr device_;
    VkBuffer buffer_ = VK_NULL_HANDLE;

    static VkBufferCreateInfo makeCreateInfo(VkDeviceSize size,
                                             VkBufferUsageFlags usage,
                                             VkBufferCreateFlags flags) {
        VkBufferCreateInfo createInfo{};
        createInfo.flags = flags;
        createInfo.size = size;
        createInfo.usage = usage;
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        return createInfo;
    }
};

using BufferPtr = std::shared_ptr<Buffer>;

}  // namespace klib::vk
