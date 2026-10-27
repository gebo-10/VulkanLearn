#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class Image {
public:
    Image(DevicePtr device, VkImageType imageType, VkFormat format,
          VkExtent3D extent, VkImageUsageFlags usage,
          VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
          VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL)
        : Image(std::move(device),
                makeCreateInfo(imageType, format, extent, usage, samples,
                               tiling)) {}

    Image(DevicePtr device, VkImageCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("Image requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        checkVk(vkCreateImage(device_->handle(), &createInfo, nullptr, &image_),
                "vkCreateImage");
    }

    ~Image() {
        if (image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_->handle(), image_, nullptr);
        }
    }

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&&) = delete;
    Image& operator=(Image&&) = delete;

    [[nodiscard]] VkImage handle() const noexcept { return image_; }
    [[nodiscard]] bool valid() const noexcept {
        return image_ != VK_NULL_HANDLE;
    }

    VkMemoryRequirements memoryRequirements() const {
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_->handle(), image_, &requirements);
        return requirements;
    }

    void bindMemory(VkDeviceMemory memory, VkDeviceSize offset = 0) const {
        checkVk(vkBindImageMemory(device_->handle(), image_, memory, offset),
                "vkBindImageMemory");
    }

private:
    DevicePtr device_;
    VkImage image_ = VK_NULL_HANDLE;

    static VkImageCreateInfo makeCreateInfo(
        VkImageType imageType, VkFormat format, VkExtent3D extent,
        VkImageUsageFlags usage, VkSampleCountFlagBits samples,
        VkImageTiling tiling) {
        VkImageCreateInfo createInfo{};
        createInfo.imageType = imageType;
        createInfo.format = format;
        createInfo.extent = extent;
        createInfo.mipLevels = 1;
        createInfo.arrayLayers = 1;
        createInfo.samples = samples;
        createInfo.tiling = tiling;
        createInfo.usage = usage;
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        return createInfo;
    }
};

using ImagePtr = std::shared_ptr<Image>;

}  // namespace klib::vk
