#pragma once

#include <kvk/command_pool.h>
#include <kvk/device.h>
#include <kvk/instance.h>
#include <kvk/surface.h>
#include <kvk/swapchain.h>
#include <sync_objects.h>

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace klib::krender {

class VulkanContext {
public:
    VulkanContext() = default;

    VulkanContext(vk::InstancePtr instance, VkSurfaceKHR ownedSurface,
                  VkExtent2D framebufferExtent, uint32_t framesInFlight = 2)
        : instance_(std::move(instance)) {
        syncObjectsFrameCount_ = framesInFlight;
        if (!instance_) {
            throw std::invalid_argument(
                "VulkanContext requires a valid Vulkan instance");
        }
        if (ownedSurface == VK_NULL_HANDLE) {
            throw std::invalid_argument(
                "VulkanContext requires a valid VkSurfaceKHR");
        }
        if (framesInFlight == 0) {
            throw std::invalid_argument(
                "VulkanContext requires at least one frame in flight");
        }

        surface_ = std::make_shared<vk::Surface>(instance_, ownedSurface);
        device_ = std::make_shared<vk::Device>(instance_, surface_);
        swapchain_ = std::make_shared<vk::Swapchain>(
            device_, surface_->handle(), framebufferExtent);
        commandPool_ = std::make_shared<vk::CommandPool>(
            device_, device_->graphicsQueueFamily(),
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
        syncObjects_ = std::make_shared<vk::SyncObjects>(device_, framesInFlight);
        commandBuffers_ = commandPool_->allocate(swapchain_->imageCount());
        imagesInFlight_.resize(swapchain_->imageCount(), VK_NULL_HANDLE);

        device = device_->handle();
        physicalDevice = device_->physicalDevice();
        graphicsQueue = device_->graphicsQueue();
        graphicsFamily = device_->graphicsQueueFamily();
        cmdPool = commandPool_->handle();
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    }

    ~VulkanContext() {
        if (device_) {
            vkDeviceWaitIdle(device_->handle());
        }
    }

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    vk::InstancePtr instanceOwner() const noexcept { return instance_; }
    vk::SurfacePtr surfaceOwner() const noexcept { return surface_; }
    vk::DevicePtr deviceOwner() const noexcept { return device_; }
    vk::SwapchainPtr swapchainOwner() const noexcept { return swapchain_; }
    vk::CommandPoolPtr commandPoolOwner() const noexcept { return commandPool_; }
    vk::SyncObjectsPtr syncObjectsOwner() const noexcept {
        return syncObjects_;
    }
    VkCommandBuffer commandBuffer(uint32_t imageIndex) const {
        return commandBuffers_.at(imageIndex);
    }
    uint32_t currentFrame() const noexcept { return currentFrame_; }
    uint32_t imageCount() const noexcept {
        return static_cast<uint32_t>(commandBuffers_.size());
    }
    VkExtent2D extent() const noexcept { return swapchain_->extent(); }

    VkResult acquireNextImage(uint32_t& imageIndex) {
        const VkFence frameFence = syncObjects_->fence(currentFrame_);
        const VkResult fenceResult = vkWaitForFences(
            device, 1, &frameFence, VK_TRUE, UINT64_MAX);
        vk::checkVk(fenceResult, "vkWaitForFences");

        const VkResult acquireResult = swapchain_->acquireNextImage(
            UINT64_MAX, syncObjects_->imageAvailable(currentFrame_),
            VK_NULL_HANDLE, imageIndex);
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            return acquireResult;
        }

        if (imageIndex >= imagesInFlight_.size()) {
            throw std::runtime_error(
                "Swapchain returned an invalid image index");
        }
        if (imagesInFlight_[imageIndex] != VK_NULL_HANDLE) {
            vk::checkVk(vkWaitForFences(device, 1, &imagesInFlight_[imageIndex],
                                        VK_TRUE, UINT64_MAX),
                        "vkWaitForFences(image)");
        }
        imagesInFlight_[imageIndex] = syncObjects_->fence(currentFrame_);
        vk::checkVk(vkResetFences(device, 1, &imagesInFlight_[imageIndex]),
                    "vkResetFences");
        vk::checkVk(vkResetCommandBuffer(commandBuffers_[imageIndex], 0),
                    "vkResetCommandBuffer");
        return acquireResult;
    }

    VkResult submitAndPresent(uint32_t imageIndex) {
        const VkCommandBuffer commandBuffer = commandBuffers_.at(imageIndex);
        const VkSemaphore imageAvailable =
            syncObjects_->imageAvailable(currentFrame_);
        const VkSemaphore renderFinished =
            syncObjects_->renderFinished(currentFrame_);
        const VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinished;
        vk::checkVk(vkQueueSubmit(graphicsQueue, 1, &submitInfo,
                                  syncObjects_->fence(currentFrame_)),
                    "vkQueueSubmit");

        const VkSwapchainKHR swapchain = swapchain_->handle();
        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;
        const VkResult result =
            vkQueuePresentKHR(device_->presentQueue(), &presentInfo);
        currentFrame_ = (currentFrame_ + 1) %
                        static_cast<uint32_t>(syncObjectsFrameCount_);
        return result;
    }

    void recreateSwapchain(VkExtent2D framebufferExtent) {
        if (framebufferExtent.width == 0 || framebufferExtent.height == 0) {
            throw std::invalid_argument(
                "Cannot recreate a zero-sized Vulkan swapchain");
        }
        device_->waitIdle();
        swapchain_->recreate(device_, framebufferExtent);
        if (!commandBuffers_.empty()) {
            vkFreeCommandBuffers(
                device, cmdPool, static_cast<uint32_t>(commandBuffers_.size()),
                commandBuffers_.data());
        }
        commandPool_->reset();
        commandBuffers_ = commandPool_->allocate(swapchain_->imageCount());
        imagesInFlight_.assign(swapchain_->imageCount(), VK_NULL_HANDLE);
    }

    uint32_t findMemoryType(uint32_t typeBits,
                            VkMemoryPropertyFlags properties) const {
        if (device_) {
            return device_->findMemoryType(typeBits, properties);
        }
        for (uint32_t index = 0; index < memProps.memoryTypeCount; ++index) {
            if ((typeBits & (1u << index)) != 0 &&
                (memProps.memoryTypes[index].propertyFlags & properties) ==
                    properties) {
                return index;
            }
        }
        throw std::runtime_error("No compatible Vulkan memory type was found");
    }

    VkCommandBuffer beginOneShot() {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandPool = cmdPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        vk::checkVk(vkAllocateCommandBuffers(device, &allocateInfo,
                                             &commandBuffer),
                    "vkAllocateCommandBuffers(one-shot)");
        VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        try {
            vk::checkVk(vkBeginCommandBuffer(commandBuffer, &beginInfo),
                        "vkBeginCommandBuffer(one-shot)");
        } catch (...) {
            vkFreeCommandBuffers(device, cmdPool, 1, &commandBuffer);
            throw;
        }
        return commandBuffer;
    }

    void endOneShot(VkCommandBuffer commandBuffer) {
        try {
            vk::checkVk(vkEndCommandBuffer(commandBuffer),
                        "vkEndCommandBuffer(one-shot)");
            VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;
            vk::checkVk(
                vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE),
                "vkQueueSubmit(one-shot)");
            vk::checkVk(vkQueueWaitIdle(graphicsQueue),
                        "vkQueueWaitIdle(one-shot)");
        } catch (...) {
            vkFreeCommandBuffers(device, cmdPool, 1, &commandBuffer);
            throw;
        }
        vkFreeCommandBuffers(device, cmdPool, 1, &commandBuffer);
    }

    void uploadToBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size,
                        const void* data) {
        if (size == 0 || data == nullptr) {
            throw std::invalid_argument(
                "Buffer upload requires non-empty data");
        }
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        createStaging(size, staging, memory);
        try {
            void* mapped = nullptr;
            vk::checkVk(vkMapMemory(device, memory, 0, size, 0, &mapped),
                        "vkMapMemory(staging buffer)");
            std::memcpy(mapped, data, static_cast<size_t>(size));
            vkUnmapMemory(device, memory);

            const VkCommandBuffer commandBuffer = beginOneShot();
            VkBufferCopy copy{0, offset, size};
            vkCmdCopyBuffer(commandBuffer, staging, buffer, 1, &copy);
            endOneShot(commandBuffer);
        } catch (...) {
            vkDestroyBuffer(device, staging, nullptr);
            vkFreeMemory(device, memory, nullptr);
            throw;
        }
        vkDestroyBuffer(device, staging, nullptr);
        vkFreeMemory(device, memory, nullptr);
    }

    void uploadToImage(VkImage image, uint32_t width, uint32_t height,
                       const void* pixels, uint32_t bytesPerRow) {
        if (width == 0 || height == 0 || pixels == nullptr ||
            static_cast<uint64_t>(bytesPerRow) <
                static_cast<uint64_t>(width) * 4 ||
            bytesPerRow % 4 != 0) {
            throw std::invalid_argument(
                "RGBA image upload requires a valid row stride and pixel data");
        }
        const VkDeviceSize size =
            static_cast<VkDeviceSize>(bytesPerRow) * height;
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        createStaging(size, staging, memory);
        try {
            void* mapped = nullptr;
            vk::checkVk(vkMapMemory(device, memory, 0, size, 0, &mapped),
                        "vkMapMemory(staging image)");
            std::memcpy(mapped, pixels, static_cast<size_t>(size));
            vkUnmapMemory(device, memory);

            const VkCommandBuffer commandBuffer = beginOneShot();
            VkImageMemoryBarrier toTransfer{
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.image = image;
            toTransfer.subresourceRange =
                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(commandBuffer,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                                 0, nullptr, 1, &toTransfer);
            VkBufferImageCopy copy{};
            copy.bufferRowLength = bytesPerRow / 4;
            copy.bufferImageHeight = height;
            copy.imageSubresource =
                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {width, height, 1};
            vkCmdCopyBufferToImage(commandBuffer, staging, image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                   &copy);
            VkImageMemoryBarrier toShader = toTransfer;
            toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &toShader);
            endOneShot(commandBuffer);
        } catch (...) {
            vkDestroyBuffer(device, staging, nullptr);
            vkFreeMemory(device, memory, nullptr);
            throw;
        }
        vkDestroyBuffer(device, staging, nullptr);
        vkFreeMemory(device, memory, nullptr);
    }

    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t graphicsFamily = 0;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memProps{};

private:
    vk::InstancePtr instance_;
    vk::SurfacePtr surface_;
    vk::DevicePtr device_;
    vk::SwapchainPtr swapchain_;
    vk::CommandPoolPtr commandPool_;
    vk::SyncObjectsPtr syncObjects_;
    std::vector<VkCommandBuffer> commandBuffers_;
    std::vector<VkFence> imagesInFlight_;
    uint32_t currentFrame_ = 0;
    size_t syncObjectsFrameCount_ = 2;

    void createStaging(VkDeviceSize size, VkBuffer& buffer,
                       VkDeviceMemory& memory) const {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vk::checkVk(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer),
                    "vkCreateBuffer(staging)");
        try {
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device, buffer, &requirements);
            VkMemoryAllocateInfo allocateInfo{
                VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocateInfo.allocationSize = requirements.size;
            allocateInfo.memoryTypeIndex = findMemoryType(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            vk::checkVk(
                vkAllocateMemory(device, &allocateInfo, nullptr, &memory),
                "vkAllocateMemory(staging)");
            vk::checkVk(vkBindBufferMemory(device, buffer, memory, 0),
                        "vkBindBufferMemory(staging)");
        } catch (...) {
            if (memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, memory, nullptr);
                memory = VK_NULL_HANDLE;
            }
            vkDestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            throw;
        }
    }
};

}  // namespace klib::krender
