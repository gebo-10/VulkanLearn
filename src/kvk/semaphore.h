#pragma once
#include <vulkan/vulkan.h>
#include <kvk/device.h>
namespace klib::vk {
class Semaphore {
public:
    Semaphore(DevicePtr device) : device_(device) {
        VkSemaphoreCreateInfo createInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        checkVk(vkCreateSemaphore(device_->handle(), &createInfo, nullptr, &semaphore_),
                "vkCreateSemaphore");
    }

    Semaphore(DevicePtr device, VkSemaphoreCreateFlags flags) : device_(device) {
        VkSemaphoreCreateInfo createInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,nullptr,flags};
        checkVk(vkCreateSemaphore(device_->handle(), &createInfo, nullptr, &semaphore_),
                "vkCreateSemaphore");
    }
    
    Semaphore(DevicePtr device, VkSemaphoreCreateInfo& createInfo) : device_(device) {
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        checkVk(vkCreateSemaphore(device_->handle(), &createInfo, nullptr, &semaphore_),
                "vkCreateSemaphore");
    }

    ~Semaphore() { 
        if (semaphore_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_->handle(), semaphore_, nullptr);
            semaphore_ = VK_NULL_HANDLE;
        }
    }

    Semaphore(const Semaphore&)            = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    Semaphore(Semaphore&&)                 = delete;   // 显式声明意图
    Semaphore& operator=(Semaphore&&)      = delete;


    [[nodiscard]] VkSemaphore handle() const noexcept { return semaphore_; }
    [[nodiscard]] bool        valid()  const noexcept { return semaphore_ != VK_NULL_HANDLE; }

private:
    DevicePtr device_ = nullptr;
    VkSemaphore semaphore_ = VK_NULL_HANDLE;
};
using SemaphorePtr = std::shared_ptr<Semaphore>;
}