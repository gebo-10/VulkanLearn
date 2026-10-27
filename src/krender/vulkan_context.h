#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace klib::krender {

struct VulkanContext {
    VkDevice         device         = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkQueue          graphicsQueue  = VK_NULL_HANDLE;
    uint32_t         graphicsFamily = 0;
    VkCommandPool    cmdPool        = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memProps{};

    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;

    VkCommandBuffer beginOneShot();
    void            endOneShot(VkCommandBuffer cmd);

    // staging → image 上传（含 barrier 与 mip0 布局转换）
    void uploadToImage(VkImage image, uint32_t w, uint32_t h,
                       const void* pixels, uint32_t bytesPerRow);
    // staging → buffer 上传
    void uploadToBuffer(VkBuffer buffer, VkDeviceSize offset,
                        VkDeviceSize size, const void* data);
};

} // namespace klib::krender


namespace klib::krender {

inline uint32_t VulkanContext::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("no suitable memory type");
}

inline VkCommandBuffer VulkanContext::beginOneShot() {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &ai, &cmd);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

inline void VulkanContext::endOneShot(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
}

// 简化：每次上传临时创建 staging（生产应使用持久 staging ring）
static inline VkBuffer createStaging(VkDevice dev, const VulkanContext& ctx,
                              VkDeviceSize size, VkDeviceMemory& mem) {
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size;
    ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buf;
    vkCreateBuffer(dev, &ci, nullptr, &buf);

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, buf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = ctx.findMemoryType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(dev, &ai, nullptr, &mem);
    vkBindBufferMemory(dev, buf, mem, 0);
    return buf;
}

inline void VulkanContext::uploadToBuffer(VkBuffer buffer, VkDeviceSize offset,
                                   VkDeviceSize size, const void* data) {
    VkDeviceMemory mem;
    VkBuffer staging = createStaging(device, *this, size, mem);
    void* ptr = nullptr;
    vkMapMemory(device, mem, 0, size, 0, &ptr);
    std::memcpy(ptr, data, size);
    vkUnmapMemory(device, mem);

    VkCommandBuffer cmd = beginOneShot();
    VkBufferCopy region{0, offset, size};
    vkCmdCopyBuffer(cmd, staging, buffer, 1, &region);
    endOneShot(cmd);

    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, mem, nullptr);
}

inline void VulkanContext::uploadToImage(VkImage image, uint32_t w, uint32_t h,
                                  const void* pixels, uint32_t bytesPerRow) {
    VkDeviceSize size = static_cast<VkDeviceSize>(bytesPerRow) * h;
    VkDeviceMemory mem;
    VkBuffer staging = createStaging(device, *this, size, mem);
    void* ptr = nullptr;
    vkMapMemory(device, mem, 0, size, 0, &ptr);
    std::memcpy(ptr, pixels, size);
    vkUnmapMemory(device, mem);

    VkCommandBuffer cmd = beginOneShot();

    VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = image;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging, image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toShader = toDst;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShader);

    endOneShot(cmd);
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, mem, nullptr);
}

} // namespace klib::krender