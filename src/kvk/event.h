#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class Event {
public:
    explicit Event(DevicePtr device)
        : Event(std::move(device),
                VkEventCreateInfo{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO}) {}

    Event(DevicePtr device, VkEventCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("Event requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
        checkVk(vkCreateEvent(device_->handle(), &createInfo, nullptr, &event_),
                "vkCreateEvent");
    }

    ~Event() {
        if (event_ != VK_NULL_HANDLE) {
            vkDestroyEvent(device_->handle(), event_, nullptr);
        }
    }

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    Event(Event&&) = delete;
    Event& operator=(Event&&) = delete;

    [[nodiscard]] VkEvent handle() const noexcept { return event_; }
    [[nodiscard]] bool valid() const noexcept {
        return event_ != VK_NULL_HANDLE;
    }

    void set() const {
        checkVk(vkSetEvent(device_->handle(), event_), "vkSetEvent");
    }

    void reset() const {
        checkVk(vkResetEvent(device_->handle(), event_), "vkResetEvent");
    }

    VkResult status() const { return vkGetEventStatus(device_->handle(), event_); }

private:
    DevicePtr device_;
    VkEvent event_ = VK_NULL_HANDLE;
};

using EventPtr = std::shared_ptr<Event>;

}  // namespace klib::vk
