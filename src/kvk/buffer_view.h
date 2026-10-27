#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class BufferView {
public:
    BufferView(DevicePtr device, VkBuffer buffer, VkFormat format,
               VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE)
        : BufferView(std::move(device),
                     makeCreateInfo(buffer, format, offset, range)) {}

    BufferView(DevicePtr device, VkBufferViewCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("BufferView requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        checkVk(vkCreateBufferView(device_->handle(), &createInfo, nullptr,
                                   &view_),
                "vkCreateBufferView");
    }

    ~BufferView() {
        if (view_ != VK_NULL_HANDLE) {
            vkDestroyBufferView(device_->handle(), view_, nullptr);
        }
    }

    BufferView(const BufferView&) = delete;
    BufferView& operator=(const BufferView&) = delete;
    BufferView(BufferView&&) = delete;
    BufferView& operator=(BufferView&&) = delete;

    [[nodiscard]] VkBufferView handle() const noexcept { return view_; }
    [[nodiscard]] bool valid() const noexcept {
        return view_ != VK_NULL_HANDLE;
    }

private:
    DevicePtr device_;
    VkBufferView view_ = VK_NULL_HANDLE;

    static VkBufferViewCreateInfo makeCreateInfo(
        VkBuffer buffer, VkFormat format, VkDeviceSize offset,
        VkDeviceSize range) {
        VkBufferViewCreateInfo createInfo{};
        createInfo.buffer = buffer;
        createInfo.format = format;
        createInfo.offset = offset;
        createInfo.range = range;
        return createInfo;
    }
};

using BufferViewPtr = std::shared_ptr<BufferView>;

}  // namespace klib::vk
