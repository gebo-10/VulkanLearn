#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class Pipeline {
public:
    Pipeline(DevicePtr device, VkGraphicsPipelineCreateInfo createInfo,
             VkPipelineCache cache = VK_NULL_HANDLE)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("Pipeline requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        checkVk(vkCreateGraphicsPipelines(device_->handle(), cache, 1,
                                          &createInfo, nullptr, &pipeline_),
                "vkCreateGraphicsPipelines");
    }

    Pipeline(DevicePtr device, VkComputePipelineCreateInfo createInfo,
             VkPipelineCache cache = VK_NULL_HANDLE)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("Pipeline requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        checkVk(vkCreateComputePipelines(device_->handle(), cache, 1,
                                         &createInfo, nullptr, &pipeline_),
                "vkCreateComputePipelines");
    }

    ~Pipeline() {
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_->handle(), pipeline_, nullptr);
        }
    }

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) = delete;
    Pipeline& operator=(Pipeline&&) = delete;

    [[nodiscard]] VkPipeline handle() const noexcept { return pipeline_; }
    [[nodiscard]] bool valid() const noexcept {
        return pipeline_ != VK_NULL_HANDLE;
    }

private:
    DevicePtr device_;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

using PipelinePtr = std::shared_ptr<Pipeline>;

}  // namespace klib::vk
