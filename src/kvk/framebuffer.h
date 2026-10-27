#pragma once

#include <kvk/device.h>

#include <memory>
#include <vector>

namespace klib::vk {

class Framebuffer {
public:
    Framebuffer(DevicePtr device, VkRenderPass renderPass,
                const std::vector<VkImageView>& attachments, uint32_t width,
                uint32_t height, uint32_t layers = 1)
        : Framebuffer(std::move(device),
                      makeCreateInfo(renderPass, attachments, width, height,
                                     layers)) {}

    Framebuffer(DevicePtr device, VkFramebufferCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("Framebuffer requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        checkVk(vkCreateFramebuffer(device_->handle(), &createInfo, nullptr,
                                    &framebuffer_),
                "vkCreateFramebuffer");
    }

    ~Framebuffer() {
        if (framebuffer_ != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_->handle(), framebuffer_, nullptr);
        }
    }

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&&) = delete;
    Framebuffer& operator=(Framebuffer&&) = delete;

    [[nodiscard]] VkFramebuffer handle() const noexcept { return framebuffer_; }
    [[nodiscard]] bool valid() const noexcept {
        return framebuffer_ != VK_NULL_HANDLE;
    }

private:
    DevicePtr device_;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

    static VkFramebufferCreateInfo makeCreateInfo(
        VkRenderPass renderPass, const std::vector<VkImageView>& attachments,
        uint32_t width, uint32_t height, uint32_t layers) {
        VkFramebufferCreateInfo createInfo{};
        createInfo.renderPass = renderPass;
        createInfo.attachmentCount =
            static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.width = width;
        createInfo.height = height;
        createInfo.layers = layers;
        return createInfo;
    }
};

using FramebufferPtr = std::shared_ptr<Framebuffer>;

}  // namespace klib::vk
