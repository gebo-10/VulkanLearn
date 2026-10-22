#pragma once

#include "renderer_config.h"
#include "vulkan/command_resources.h"
#include "vulkan/graphics_pipeline.h"
#include "vulkan/swapchain.h"
#include "vulkan/sync_objects.h"

#include <spdlog/spdlog.h>

#include <cstdint>

namespace vulkanlearn {

class Renderer {
public:
    explicit Renderer(GLFWwindow* window) : window_(window) {}

    ~Renderer() {
        if (device_.handle() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_.handle());
        }
    }

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void initialize() {
        instance_.initialize(window_);
        device_.initialize(instance_);
        swapchain_.initialize(device_, instance_.surface(), window_);
        pipeline_.initialize(device_.handle(), swapchain_.imageFormat(),
                             PipelineConfig::triangle());
        commandResources_.initialize(device_, swapchain_.imageCount());
        syncObjects_.initialize(device_.handle());
    }

    void drawFrame() {
        const VkFence frameFence = syncObjects_.fence(currentFrame_);
        checkVk(vkWaitForFences(device_.handle(), 1, &frameFence, VK_TRUE,
                                UINT64_MAX),
                "vkWaitForFences");

        uint32_t imageIndex = 0;
        const VkResult acquireResult = vkAcquireNextImageKHR(
            device_.handle(), swapchain_.handle(), UINT64_MAX,
            syncObjects_.imageAvailable(currentFrame_), VK_NULL_HANDLE,
            &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        }
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            checkVk(acquireResult, "vkAcquireNextImageKHR");
        }

        checkVk(vkResetFences(device_.handle(), 1, &frameFence), "vkResetFences");
        VkCommandBuffer commandBuffer = commandResources_.handle(imageIndex);
        checkVk(vkResetCommandBuffer(commandBuffer, 0), "vkResetCommandBuffer");
        recordCommandBuffer(commandBuffer, imageIndex);

        const VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkSemaphore imageAvailable =
            syncObjects_.imageAvailable(currentFrame_);
        const VkSemaphore renderFinished =
            syncObjects_.renderFinished(currentFrame_);
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinished;
        checkVk(vkQueueSubmit(device_.graphicsQueue(), 1, &submitInfo, frameFence),
                "vkQueueSubmit");

        const VkSwapchainKHR swapchain = swapchain_.handle();
        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;

        const VkResult presentResult =
            vkQueuePresentKHR(device_.presentQueue(), &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
            presentResult == VK_SUBOPTIMAL_KHR || framebufferResized_) {
            framebufferResized_ = false;
            recreateSwapchain();
        } else {
            checkVk(presentResult, "vkQueuePresentKHR");
        }
        currentFrame_ = (currentFrame_ + 1) % kFramesInFlight;
    }

    static void framebufferSizeCallback(GLFWwindow* window, int, int) {
        auto* renderer =
            static_cast<Renderer*>(glfwGetWindowUserPointer(window));
        if (renderer != nullptr) {
            renderer->framebufferResized_ = true;
        }
    }

private:
    GLFWwindow* window_ = nullptr;
    VulkanInstance instance_;
    VulkanDevice device_;
    Swapchain swapchain_;
    GraphicsPipeline pipeline_;
    CommandResources commandResources_;
    SyncObjects syncObjects_;
    uint32_t currentFrame_ = 0;
    bool framebufferResized_ = false;

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
        VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        checkVk(vkBeginCommandBuffer(commandBuffer, &beginInfo),
                "vkBeginCommandBuffer");

        VkImageMemoryBarrier toColorAttachment{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toColorAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toColorAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toColorAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachment.image = swapchain_.images()[imageIndex];
        toColorAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toColorAttachment.subresourceRange.levelCount = 1;
        toColorAttachment.subresourceRange.layerCount = 1;
        toColorAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &toColorAttachment);

        VkRenderingAttachmentInfo colorAttachment{
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        colorAttachment.imageView = swapchain_.imageViews()[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = {{0.04f, 0.05f, 0.08f, 1.0f}};

        const VkExtent2D extent = swapchain_.extent();
        VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
        renderingInfo.renderArea = {{0, 0}, extent};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_.handle());

        VkViewport viewport{};
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.maxDepth = 1.0f;
        const VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRendering(commandBuffer);

        VkImageMemoryBarrier toPresent{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image = swapchain_.images()[imageIndex];
        toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toPresent.subresourceRange.levelCount = 1;
        toPresent.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &toPresent);
        checkVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
    }

    void recreateSwapchain() {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        while (width == 0 || height == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(window_, &width, &height);
        }

        device_.waitIdle();
        swapchain_.recreate();
        pipeline_.recreate(swapchain_.imageFormat());
        commandResources_.recreate(swapchain_.imageCount());
    }
};

}  // namespace vulkanlearn
