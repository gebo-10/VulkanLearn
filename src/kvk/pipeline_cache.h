#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class PipelineCache {
public:
    PipelineCache(DevicePtr device, VkPipelineCacheCreateInfo createInfo)
        : device_(std::move(device)) {
        if (!device_) {
            throw std::invalid_argument("PipelineCache requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        checkVk(vkCreatePipelineCache(device_->handle(), &createInfo, nullptr,
                                      &cache_),
                "vkCreatePipelineCache");
    }

    ~PipelineCache() {
        if (cache_ != VK_NULL_HANDLE) {
            vkDestroyPipelineCache(device_->handle(), cache_, nullptr);
        }
    }

    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;
    PipelineCache(PipelineCache&&) = delete;
    PipelineCache& operator=(PipelineCache&&) = delete;

    [[nodiscard]] VkPipelineCache handle() const noexcept { return cache_; }
    [[nodiscard]] bool valid() const noexcept {
        return cache_ != VK_NULL_HANDLE;
    }

    std::vector<std::byte> data() const {
        size_t size = 0;
        checkVk(vkGetPipelineCacheData(device_->handle(), cache_, &size, nullptr),
                "vkGetPipelineCacheData");
        std::vector<std::byte> bytes(size);
        checkVk(vkGetPipelineCacheData(device_->handle(), cache_, &size,
                                       bytes.data()),
                "vkGetPipelineCacheData");
        bytes.resize(size);
        return bytes;
    }

private:
    DevicePtr device_;
    VkPipelineCache cache_ = VK_NULL_HANDLE;
};

using PipelineCachePtr = std::shared_ptr<PipelineCache>;

}  // namespace klib::vk
