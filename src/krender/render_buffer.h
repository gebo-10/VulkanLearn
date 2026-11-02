#pragma once

#include "resource_base.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace klib::krender {

class RenderBuffer : public ResourceBase {
public:
    RenderBuffer(Handle handle, const void* data, VkDeviceSize size,
                 VkBufferUsageFlags usage)
        : ResourceBase(handle), usage_(usage) {
        if (usage_ == 0) {
            throw std::invalid_argument(
                "RenderBuffer requires at least one buffer usage flag");
        }
        setData(data, size, false);
    }

    void update(const void* data, VkDeviceSize size) {
        setData(data, size, true);
    }

    const void* data() const noexcept { return data_.data(); }
    VkDeviceSize size() const noexcept {
        return static_cast<VkDeviceSize>(data_.size());
    }
    VkBufferUsageFlags usage() const noexcept { return usage_; }
    uint64_t sizeBytes() const noexcept override { return data_.size(); }

private:
    std::vector<std::byte> data_;
    VkBufferUsageFlags usage_;

    void setData(const void* data, VkDeviceSize size, bool updateVersion) {
        if (data == nullptr || size == 0) {
            throw std::invalid_argument(
                "RenderBuffer requires non-empty source data");
        }
        if (size > std::numeric_limits<size_t>::max()) {
            throw std::length_error("RenderBuffer is too large for host memory");
        }
        std::vector<std::byte> replacement(static_cast<size_t>(size));
        std::memcpy(replacement.data(), data, replacement.size());
        data_ = std::move(replacement);
        if (updateVersion) {
            bumpVersion();
        }
    }
};

using RenderBufferPtr = std::shared_ptr<RenderBuffer>;

template <>
struct ResourceTraits<RenderBuffer> {
    static constexpr ZeroRefPolicy zeroRef = ZeroRefPolicy::Evictable;
    static constexpr ResourceKind kind = ResourceKind::RenderBuffer;
};

}  // namespace klib::krender