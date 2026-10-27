#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class PrivateDataSlot {
public:
    PrivateDataSlot(DevicePtr device,
                    VkPrivateDataSlotCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument(
                "PrivateDataSlot requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_PRIVATE_DATA_SLOT_CREATE_INFO;
        checkVk(vkCreatePrivateDataSlot(device_->handle(), &createInfo, nullptr,
                                        &slot_),
                "vkCreatePrivateDataSlot");
    }

    ~PrivateDataSlot() {
        if (slot_ != VK_NULL_HANDLE) {
            vkDestroyPrivateDataSlot(device_->handle(), slot_, nullptr);
        }
    }

    PrivateDataSlot(const PrivateDataSlot&) = delete;
    PrivateDataSlot& operator=(const PrivateDataSlot&) = delete;
    PrivateDataSlot(PrivateDataSlot&&) = delete;
    PrivateDataSlot& operator=(PrivateDataSlot&&) = delete;

    [[nodiscard]] VkPrivateDataSlot handle() const noexcept { return slot_; }
    [[nodiscard]] bool valid() const noexcept {
        return slot_ != VK_NULL_HANDLE;
    }

    void set(VkObjectType objectType, uint64_t objectHandle,
             uint64_t data) const {
        checkVk(vkSetPrivateData(device_->handle(), objectType, objectHandle,
                                 slot_, data),
                "vkSetPrivateData");
    }

    uint64_t get(VkObjectType objectType, uint64_t objectHandle) const {
        uint64_t data = 0;
        vkGetPrivateData(device_->handle(), objectType, objectHandle, slot_,
                         &data);
        return data;
    }

private:
    DevicePtr device_;
    VkPrivateDataSlot slot_ = VK_NULL_HANDLE;
};

using PrivateDataSlotPtr = std::shared_ptr<PrivateDataSlot>;

}  // namespace klib::vk
