#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class ImageView {
public:
    ImageView(DevicePtr device, VkImage image, VkFormat format,
              VkImageAspectFlags aspectMask,
              VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D,
              uint32_t baseMipLevel = 0, uint32_t levelCount = 1,
              uint32_t baseArrayLayer = 0, uint32_t layerCount = 1)
        : ImageView(std::move(device),
                    makeCreateInfo(image, format, aspectMask, viewType,
                                   baseMipLevel, levelCount, baseArrayLayer,
                                   layerCount)) {}

    ImageView(DevicePtr device, VkImageViewCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("ImageView requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        checkVk(vkCreateImageView(device_->handle(), &createInfo, nullptr,
                                  &imageView_),
                "vkCreateImageView");
    }

    ~ImageView() {
        if (imageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_->handle(), imageView_, nullptr);
        }
    }

    ImageView(const ImageView&) = delete;
    ImageView& operator=(const ImageView&) = delete;
    ImageView(ImageView&&) = delete;
    ImageView& operator=(ImageView&&) = delete;

    [[nodiscard]] VkImageView handle() const noexcept { return imageView_; }
    [[nodiscard]] bool valid() const noexcept {
        return imageView_ != VK_NULL_HANDLE;
    }

private:
    DevicePtr device_;
    VkImageView imageView_ = VK_NULL_HANDLE;

    static VkImageViewCreateInfo makeCreateInfo(
        VkImage image, VkFormat format, VkImageAspectFlags aspectMask,
        VkImageViewType viewType, uint32_t baseMipLevel, uint32_t levelCount,
        uint32_t baseArrayLayer, uint32_t layerCount) {
        VkImageViewCreateInfo createInfo{};
        createInfo.image = image;
        createInfo.viewType = viewType;
        createInfo.format = format;
        createInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY,
                                 VK_COMPONENT_SWIZZLE_IDENTITY};
        createInfo.subresourceRange.aspectMask = aspectMask;
        createInfo.subresourceRange.baseMipLevel = baseMipLevel;
        createInfo.subresourceRange.levelCount = levelCount;
        createInfo.subresourceRange.baseArrayLayer = baseArrayLayer;
        createInfo.subresourceRange.layerCount = layerCount;
        return createInfo;
    }
};

using ImageViewPtr = std::shared_ptr<ImageView>;

}  // namespace klib::vk
