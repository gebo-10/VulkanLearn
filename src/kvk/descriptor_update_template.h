#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class DescriptorUpdateTemplate {
public:
    DescriptorUpdateTemplate(
        DevicePtr device,
        VkDescriptorUpdateTemplateCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument(
                "DescriptorUpdateTemplate requires a valid Device");
        }
        createInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
        checkVk(vkCreateDescriptorUpdateTemplate(
                    device_->handle(), &createInfo, nullptr, &template_),
                "vkCreateDescriptorUpdateTemplate");
    }

    ~DescriptorUpdateTemplate() {
        if (template_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorUpdateTemplate(device_->handle(), template_,
                                              nullptr);
        }
    }

    DescriptorUpdateTemplate(const DescriptorUpdateTemplate&) = delete;
    DescriptorUpdateTemplate& operator=(const DescriptorUpdateTemplate&) =
        delete;
    DescriptorUpdateTemplate(DescriptorUpdateTemplate&&) = delete;
    DescriptorUpdateTemplate& operator=(DescriptorUpdateTemplate&&) = delete;

    [[nodiscard]] VkDescriptorUpdateTemplate handle() const noexcept {
        return template_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return template_ != VK_NULL_HANDLE;
    }

    void update(VkDescriptorSet descriptorSet, const void* data) const {
        vkUpdateDescriptorSetWithTemplate(device_->handle(), descriptorSet,
                                          template_, data);
    }

private:
    DevicePtr device_;
    VkDescriptorUpdateTemplate template_ = VK_NULL_HANDLE;
};

using DescriptorUpdateTemplatePtr =
    std::shared_ptr<DescriptorUpdateTemplate>;

}  // namespace klib::vk
