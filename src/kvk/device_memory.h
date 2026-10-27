#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class DeviceMemory {
public:
    DeviceMemory(DevicePtr device, VkDeviceSize allocationSize,
                 uint32_t memoryTypeIndex)
        : DeviceMemory(std::move(device),
                       makeAllocateInfo(allocationSize, memoryTypeIndex)) {}

    DeviceMemory(DevicePtr device, VkMemoryAllocateInfo allocateInfo)
        : device_(std::move(device)), size_(allocateInfo.allocationSize) {
        if (!device_) {
            throw std::invalid_argument("DeviceMemory requires a valid Device");
        }
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        checkVk(vkAllocateMemory(device_->handle(), &allocateInfo, nullptr,
                                 &memory_),
                "vkAllocateMemory");
    }

    ~DeviceMemory() {
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_->handle(), memory_, nullptr);
        }
    }

    DeviceMemory(const DeviceMemory&) = delete;
    DeviceMemory& operator=(const DeviceMemory&) = delete;
    DeviceMemory(DeviceMemory&&) = delete;
    DeviceMemory& operator=(DeviceMemory&&) = delete;

    [[nodiscard]] VkDeviceMemory handle() const noexcept { return memory_; }
    [[nodiscard]] VkDeviceSize size() const noexcept { return size_; }
    [[nodiscard]] bool valid() const noexcept {
        return memory_ != VK_NULL_HANDLE;
    }

    void* map(VkDeviceSize offset = 0,
              VkDeviceSize mapSize = VK_WHOLE_SIZE,
              VkMemoryMapFlags flags = 0) const {
        void* mapped = nullptr;
        checkVk(vkMapMemory(device_->handle(), memory_, offset, mapSize, flags,
                            &mapped),
                "vkMapMemory");
        return mapped;
    }

    void unmap() const { vkUnmapMemory(device_->handle(), memory_); }

private:
    DevicePtr device_;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;

    static VkMemoryAllocateInfo makeAllocateInfo(
        VkDeviceSize allocationSize, uint32_t memoryTypeIndex) {
        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.allocationSize = allocationSize;
        allocateInfo.memoryTypeIndex = memoryTypeIndex;
        return allocateInfo;
    }
};

using DeviceMemoryPtr = std::shared_ptr<DeviceMemory>;

}  // namespace klib::vk
