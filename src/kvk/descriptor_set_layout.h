#pragma once

#include <kvk/device.h>

#include <memory>
#include <vector>

namespace klib::vk {

class DescriptorSetLayout {
public:
    DescriptorSetLayout(
        DevicePtr device,
        const std::vector<VkDescriptorSetLayoutBinding>& bindings,
        VkDescriptorSetLayoutCreateFlags flags = 0)
        : DescriptorSetLayout(std::move(device),
                              makeCreateInfo(bindings, flags)) {}

    DescriptorSetLayout(DevicePtr device,
                        VkDescriptorSetLayoutCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument(
                "DescriptorSetLayout requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        checkVk(vkCreateDescriptorSetLayout(device_->handle(), &createInfo,
                                            nullptr, &layout_),
                "vkCreateDescriptorSetLayout");
    }

    ~DescriptorSetLayout() {
        if (layout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_->handle(), layout_, nullptr);
        }
    }

    DescriptorSetLayout(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout(DescriptorSetLayout&&) = delete;
    DescriptorSetLayout& operator=(DescriptorSetLayout&&) = delete;

    [[nodiscard]] VkDescriptorSetLayout handle() const noexcept {
        return layout_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return layout_ != VK_NULL_HANDLE;
    }

private:
    DevicePtr device_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;

    static VkDescriptorSetLayoutCreateInfo makeCreateInfo(
        const std::vector<VkDescriptorSetLayoutBinding>& bindings,
        VkDescriptorSetLayoutCreateFlags flags) {
        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.flags = flags;
        createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        createInfo.pBindings = bindings.data();
        return createInfo;
    }
};

using DescriptorSetLayoutPtr = std::shared_ptr<DescriptorSetLayout>;

}  // namespace klib::vk
