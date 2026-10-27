#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class RenderPass {
public:
    RenderPass(DevicePtr device, VkRenderPassCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("RenderPass requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        checkVk(vkCreateRenderPass(device_->handle(), &createInfo, nullptr,
                                   &renderPass_),
                "vkCreateRenderPass");
    }

    RenderPass(DevicePtr device,
               VkRenderPassCreateInfo2 createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("RenderPass requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
        checkVk(vkCreateRenderPass2(device_->handle(), &createInfo, nullptr,
                                    &renderPass_),
                "vkCreateRenderPass2");
    }

    ~RenderPass() {
        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_->handle(), renderPass_, nullptr);
        }
    }

    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;
    RenderPass(RenderPass&&) = delete;
    RenderPass& operator=(RenderPass&&) = delete;

    [[nodiscard]] VkRenderPass handle() const noexcept { return renderPass_; }
    [[nodiscard]] bool valid() const noexcept {
        return renderPass_ != VK_NULL_HANDLE;
    }

private:
    DevicePtr device_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
};

using RenderPassPtr = std::shared_ptr<RenderPass>;

}  // namespace klib::vk
