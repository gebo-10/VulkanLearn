#pragma once

#include "resource_base.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>

namespace klib::krender {

namespace vulkanimpl {
class VulkanRenderImpl;
}

class RenderTexture : public ResourceBase {
public:
    RenderTexture(Handle handle, uint32_t width, uint32_t height,
                  VkFormat format = VK_FORMAT_R8G8B8A8_UNORM,
                  VkImageUsageFlags usage =
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT,
                  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT)
        : ResourceBase(handle),
          width_(width),
          height_(height),
          format_(format),
          usage_(usage),
          aspect_(aspect) {
        if (width_ == 0 || height_ == 0) {
            throw std::invalid_argument(
                "RenderTexture dimensions must be positive");
        }
        if (format_ == VK_FORMAT_UNDEFINED || usage_ == 0 || aspect_ == 0) {
            throw std::invalid_argument(
                "RenderTexture requires a format, usage, and aspect");
        }
        if (static_cast<uint64_t>(width_) * height_ >
            std::numeric_limits<uint32_t>::max()) {
            throw std::length_error("RenderTexture dimensions are too large");
        }
    }

    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }
    VkFormat format() const noexcept { return format_; }
    VkImageUsageFlags usage() const noexcept { return usage_; }
    VkImageAspectFlags aspect() const noexcept { return aspect_; }

    // Available after this texture has been resolved by the Vulkan renderer.
    VkImageView nativeHandle() const noexcept { return nativeHandle_; }
    VkImageLayout nativeLayout() const noexcept { return nativeLayout_; }

    uint64_t sizeBytes() const noexcept override {
        return static_cast<uint64_t>(width_) * height_ * 4;
    }

private:
    friend class vulkanimpl::VulkanRenderImpl;

    uint32_t width_;
    uint32_t height_;
    VkFormat format_;
    VkImageUsageFlags usage_;
    VkImageAspectFlags aspect_;
    VkImageView nativeHandle_ = VK_NULL_HANDLE;
    VkImageLayout nativeLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    void setNativeState(VkImageView view, VkImageLayout layout) noexcept {
        nativeHandle_ = view;
        nativeLayout_ = layout;
    }
};

using RenderTexturePtr = std::shared_ptr<RenderTexture>;

template <>
struct ResourceTraits<RenderTexture> {
    static constexpr ZeroRefPolicy zeroRef = ZeroRefPolicy::Evictable;
    static constexpr ResourceKind kind = ResourceKind::RenderTexture;
};

}  // namespace klib::krender
