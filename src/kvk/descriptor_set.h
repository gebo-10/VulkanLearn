#pragma once

#include <kvk/descriptor_pool.h>

#include <memory>
#include <spdlog/spdlog.h>

namespace klib::vk {

class DescriptorSet {
public:
    DescriptorSet(DescriptorPoolPtr pool, VkDescriptorSetLayout layout)
        : pool_(std::move(pool)) {
        if (!pool_) {
            throw std::invalid_argument(
                "DescriptorSet requires a valid DescriptorPool");
        }
        descriptorSet_ = pool_->allocate(layout);
    }

    ~DescriptorSet() {
        if (descriptorSet_ != VK_NULL_HANDLE && pool_ &&
            (pool_->flags() &
             VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT) != 0) {
            const VkResult result = vkFreeDescriptorSets(
                pool_->deviceHandle(), pool_->handle(), 1, &descriptorSet_);
            if (result != VK_SUCCESS) {
                spdlog::error("vkFreeDescriptorSets failed (VkResult {})",
                              static_cast<int>(result));
            }
        }
    }

    DescriptorSet(const DescriptorSet&) = delete;
    DescriptorSet& operator=(const DescriptorSet&) = delete;
    DescriptorSet(DescriptorSet&&) = delete;
    DescriptorSet& operator=(DescriptorSet&&) = delete;

    [[nodiscard]] VkDescriptorSet handle() const noexcept {
        return descriptorSet_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return descriptorSet_ != VK_NULL_HANDLE;
    }

    void update(uint32_t writeCount, const VkWriteDescriptorSet* writes,
                uint32_t copyCount = 0,
                const VkCopyDescriptorSet* copies = nullptr) const {
        pool_->update(writeCount, writes, copyCount, copies);
    }

private:
    DescriptorPoolPtr pool_;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
};

using DescriptorSetPtr = std::shared_ptr<DescriptorSet>;

}  // namespace klib::vk
