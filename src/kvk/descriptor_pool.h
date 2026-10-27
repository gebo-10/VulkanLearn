#pragma once

#include <kvk/device.h>

#include <memory>
#include <vector>

namespace klib::vk {

class DescriptorPool {
public:
    DescriptorPool(DevicePtr device, uint32_t maxSets,
                   const std::vector<VkDescriptorPoolSize>& poolSizes,
                   VkDescriptorPoolCreateFlags flags = 0)
        : DescriptorPool(std::move(device),
                         makeCreateInfo(maxSets, poolSizes, flags)) {}

    DescriptorPool(DevicePtr device, VkDescriptorPoolCreateInfo createInfo)
        : device_(std::move(device)), flags_(createInfo.flags) {
        if (!device_) {
            throw std::invalid_argument("DescriptorPool requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        checkVk(vkCreateDescriptorPool(device_->handle(), &createInfo, nullptr,
                                       &pool_),
                "vkCreateDescriptorPool");
    }

    ~DescriptorPool() {
        if (pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_->handle(), pool_, nullptr);
        }
    }

    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;
    DescriptorPool(DescriptorPool&&) = delete;
    DescriptorPool& operator=(DescriptorPool&&) = delete;

    [[nodiscard]] VkDescriptorPool handle() const noexcept { return pool_; }
    [[nodiscard]] VkDevice deviceHandle() const noexcept {
        return device_->handle();
    }
    [[nodiscard]] VkDescriptorPoolCreateFlags flags() const noexcept {
        return flags_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return pool_ != VK_NULL_HANDLE;
    }

    void reset(VkDescriptorPoolResetFlags flags = 0) const {
        checkVk(vkResetDescriptorPool(device_->handle(), pool_, flags),
                "vkResetDescriptorPool");
    }

    void update(uint32_t writeCount, const VkWriteDescriptorSet* writes,
                uint32_t copyCount = 0,
                const VkCopyDescriptorSet* copies = nullptr) const {
        vkUpdateDescriptorSets(device_->handle(), writeCount, writes,
                               copyCount, copies);
    }

    void free(VkDescriptorSet descriptorSet) const {
        if ((flags_ & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT) == 0) {
            throw std::logic_error(
                "DescriptorPool was not created with free-descriptor-set support");
        }
        checkVk(vkFreeDescriptorSets(device_->handle(), pool_, 1,
                                     &descriptorSet),
                "vkFreeDescriptorSets");
    }

    VkDescriptorSet allocate(VkDescriptorSetLayout layout) const {
        VkDescriptorSetAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocateInfo.descriptorPool = pool_;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &layout;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        checkVk(vkAllocateDescriptorSets(device_->handle(), &allocateInfo,
                                         &descriptorSet),
                "vkAllocateDescriptorSets");
        return descriptorSet;
    }

    std::vector<VkDescriptorSet> allocate(
        const std::vector<VkDescriptorSetLayout>& layouts) const {
        if (layouts.empty()) {
            return {};
        }
        VkDescriptorSetAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocateInfo.descriptorPool = pool_;
        allocateInfo.descriptorSetCount =
            static_cast<uint32_t>(layouts.size());
        allocateInfo.pSetLayouts = layouts.data();
        std::vector<VkDescriptorSet> descriptorSets(layouts.size());
        checkVk(vkAllocateDescriptorSets(device_->handle(), &allocateInfo,
                                         descriptorSets.data()),
                "vkAllocateDescriptorSets");
        return descriptorSets;
    }

private:
    DevicePtr device_;
    VkDescriptorPoolCreateFlags flags_ = 0;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;

    static VkDescriptorPoolCreateInfo makeCreateInfo(
        uint32_t maxSets,
        const std::vector<VkDescriptorPoolSize>& poolSizes,
        VkDescriptorPoolCreateFlags flags) {
        VkDescriptorPoolCreateInfo createInfo{};
        createInfo.flags = flags;
        createInfo.maxSets = maxSets;
        createInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        createInfo.pPoolSizes = poolSizes.data();
        return createInfo;
    }
};

using DescriptorPoolPtr = std::shared_ptr<DescriptorPool>;

}  // namespace klib::vk
