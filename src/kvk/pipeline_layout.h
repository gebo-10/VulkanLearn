#pragma once

#include <kvk/device.h>

#include <memory>
#include <vector>

namespace klib::vk {

class PipelineLayout {
public:
    PipelineLayout(
        DevicePtr device,
        const std::vector<VkDescriptorSetLayout>& setLayouts,
        const std::vector<VkPushConstantRange>& pushConstantRanges = {})
        : PipelineLayout(std::move(device),
                         makeCreateInfo(setLayouts, pushConstantRanges)) {}

    PipelineLayout(DevicePtr device,
                   VkPipelineLayoutCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("PipelineLayout requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        checkVk(vkCreatePipelineLayout(device_->handle(), &createInfo, nullptr,
                                       &layout_),
                "vkCreatePipelineLayout");
    }

    ~PipelineLayout() {
        if (layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_->handle(), layout_, nullptr);
        }
    }

    PipelineLayout(const PipelineLayout&) = delete;
    PipelineLayout& operator=(const PipelineLayout&) = delete;
    PipelineLayout(PipelineLayout&&) = delete;
    PipelineLayout& operator=(PipelineLayout&&) = delete;

    [[nodiscard]] VkPipelineLayout handle() const noexcept { return layout_; }
    [[nodiscard]] bool valid() const noexcept {
        return layout_ != VK_NULL_HANDLE;
    }

private:
    DevicePtr device_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;

    static VkPipelineLayoutCreateInfo makeCreateInfo(
        const std::vector<VkDescriptorSetLayout>& setLayouts,
        const std::vector<VkPushConstantRange>& pushConstantRanges) {
        VkPipelineLayoutCreateInfo createInfo{};
        createInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        createInfo.pSetLayouts = setLayouts.data();
        createInfo.pushConstantRangeCount =
            static_cast<uint32_t>(pushConstantRanges.size());
        createInfo.pPushConstantRanges = pushConstantRanges.data();
        return createInfo;
    }
};

using PipelineLayoutPtr = std::shared_ptr<PipelineLayout>;

}  // namespace klib::vk
