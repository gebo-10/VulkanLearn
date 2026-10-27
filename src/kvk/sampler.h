#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class Sampler {
public:
    Sampler(DevicePtr device, VkSamplerCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("Sampler requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        checkVk(vkCreateSampler(device_->handle(), &createInfo, nullptr,
                                &sampler_),
                "vkCreateSampler");
    }

    Sampler(DevicePtr device, VkFilter filter = VK_FILTER_LINEAR,
            VkSamplerAddressMode addressMode =
                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
        : Sampler(std::move(device), makeCreateInfo(filter, addressMode)) {}

    ~Sampler() {
        if (sampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_->handle(), sampler_, nullptr);
        }
    }

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;
    Sampler(Sampler&&) = delete;
    Sampler& operator=(Sampler&&) = delete;

    [[nodiscard]] VkSampler handle() const noexcept { return sampler_; }
    [[nodiscard]] bool valid() const noexcept {
        return sampler_ != VK_NULL_HANDLE;
    }

private:
    DevicePtr device_;
    VkSampler sampler_ = VK_NULL_HANDLE;

    static VkSamplerCreateInfo makeCreateInfo(
        VkFilter filter, VkSamplerAddressMode addressMode) {
        VkSamplerCreateInfo createInfo{};
        createInfo.magFilter = filter;
        createInfo.minFilter = filter;
        createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        createInfo.addressModeU = addressMode;
        createInfo.addressModeV = addressMode;
        createInfo.addressModeW = addressMode;
        createInfo.maxAnisotropy = 1.0f;
        createInfo.maxLod = 0.0f;
        createInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        return createInfo;
    }
};

using SamplerPtr = std::shared_ptr<Sampler>;

}  // namespace klib::vk
