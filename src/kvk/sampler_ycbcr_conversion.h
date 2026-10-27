#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class SamplerYcbcrConversion {
public:
    SamplerYcbcrConversion(
        DevicePtr device,
        VkSamplerYcbcrConversionCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument(
                "SamplerYcbcrConversion requires a valid Device");
        }
        createInfo.sType =
            VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
        checkVk(vkCreateSamplerYcbcrConversion(device_->handle(), &createInfo,
                                              nullptr, &conversion_),
                "vkCreateSamplerYcbcrConversion");
    }

    ~SamplerYcbcrConversion() {
        if (conversion_ != VK_NULL_HANDLE) {
            vkDestroySamplerYcbcrConversion(device_->handle(), conversion_,
                                            nullptr);
        }
    }

    SamplerYcbcrConversion(const SamplerYcbcrConversion&) = delete;
    SamplerYcbcrConversion& operator=(const SamplerYcbcrConversion&) = delete;
    SamplerYcbcrConversion(SamplerYcbcrConversion&&) = delete;
    SamplerYcbcrConversion& operator=(SamplerYcbcrConversion&&) = delete;

    [[nodiscard]] VkSamplerYcbcrConversion handle() const noexcept {
        return conversion_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return conversion_ != VK_NULL_HANDLE;
    }

private:
    DevicePtr device_;
    VkSamplerYcbcrConversion conversion_ = VK_NULL_HANDLE;
};

using SamplerYcbcrConversionPtr =
    std::shared_ptr<SamplerYcbcrConversion>;

}  // namespace klib::vk
