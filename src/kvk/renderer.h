#pragma once

#include "renderer_config.h"
#include <kvk/command_resources.h>
#include <kvk/graphics_pipeline.h>
#include <kvk/swapchain.h>
#include <kvk/sync_objects.h>
#include <kvk/window.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstring>

struct GLFWwindow;

namespace klib::vk {

class Renderer {
public:
    explicit Renderer(Window& window) : window_(window) {}

    ~Renderer() {
        window_.setResizeCallback({});
        if (device_.handle() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_.handle());
        }
        shutdownImGui();
        destroyVertexBuffer();
    }

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void initialize() {
        instance_.initialize(window_);
        device_.initialize(instance_);
        swapchain_.initialize(device_, instance_.surface(), window_);
        createVertexBuffer();
        pipeline_.initialize(device_.handle(), swapchain_.imageFormat(),
                             PipelineConfig::triangle());
        commandResources_.initialize(device_, swapchain_.imageCount());
        syncObjects_.initialize(device_.handle());
        window_.setResizeCallback([this] { framebufferResized_ = true; });
    }

    void initializeImGui(GLFWwindow* window) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        imguiContextCreated_ = true;
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard |
                          ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();

        // ImGuiIO& io = ImGui::GetIO();
        // io.ConfigDpiScaleFonts = true;

        float dpi_scale = 2.0f;
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(dpi_scale); // 缩放内边距、间距、边框等
        style.FontScaleDpi = dpi_scale; // 缩放字体

        ImVec4* colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_WindowBg]               = ImVec4(0.18f, 0.18f, 0.18f, 0.70f);





        if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
            throw std::runtime_error("Failed to initialize ImGui GLFW backend");
        }
        imguiGlfwInitialized_ = true;

        VkPipelineRenderingCreateInfo renderingInfo{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        const VkFormat colorFormat = swapchain_.imageFormat();
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_4;
        initInfo.Instance = instance_.handle();
        initInfo.PhysicalDevice = device_.physicalDevice();
        initInfo.Device = device_.handle();
        initInfo.QueueFamily = device_.graphicsQueueFamily();
        initInfo.Queue = device_.graphicsQueue();
        initInfo.DescriptorPoolSize = 1000;
        initInfo.MinImageCount = 2;
        initInfo.ImageCount = swapchain_.imageCount();
        initInfo.UseDynamicRendering = true;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = renderingInfo;
        if (!ImGui_ImplVulkan_Init(&initInfo)) {
            throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
        }
        imguiVulkanInitialized_ = true;
    }

    void drawFrame(ImDrawData* drawData) {
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
        recordCommandBuffer(commandBuffer, imageIndex, drawData);

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

private:
    Window& window_;
    Instance instance_;
    Device device_;
    Swapchain swapchain_;
    GraphicsPipeline pipeline_;
    CommandResources commandResources_;
    SyncObjects syncObjects_;
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory_ = VK_NULL_HANDLE;
    uint32_t vertexCount_ = 0;
    uint32_t currentFrame_ = 0;
    bool framebufferResized_ = false;
    bool imguiContextCreated_ = false;
    bool imguiGlfwInitialized_ = false;
    bool imguiVulkanInitialized_ = false;

    void createVertexBuffer() {
        constexpr Vertex vertices[] = {
            {{0.0f, -0.65f}, {1.0f, 0.2f, 0.2f}},
            {{0.65f, 0.55f}, {0.2f, 1.0f, 0.3f}},
            {{-0.65f, 0.55f}, {0.2f, 0.4f, 1.0f}}};
        const VkDeviceSize bufferSize = sizeof(vertices);

        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        checkVk(vkCreateBuffer(device_.handle(), &bufferInfo, nullptr,
                               &vertexBuffer_),
                "vkCreateBuffer");

        VkMemoryRequirements memoryRequirements{};
        vkGetBufferMemoryRequirements(device_.handle(), vertexBuffer_,
                                      &memoryRequirements);
        VkMemoryAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = device_.findMemoryType(
            memoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        checkVk(vkAllocateMemory(device_.handle(), &allocateInfo, nullptr,
                                 &vertexBufferMemory_),
                "vkAllocateMemory");
        checkVk(vkBindBufferMemory(device_.handle(), vertexBuffer_,
                                   vertexBufferMemory_, 0),
                "vkBindBufferMemory");

        void* mappedMemory = nullptr;
        checkVk(vkMapMemory(device_.handle(), vertexBufferMemory_, 0, bufferSize,
                            0, &mappedMemory),
                "vkMapMemory");
        std::memcpy(mappedMemory, vertices, sizeof(vertices));
        vkUnmapMemory(device_.handle(), vertexBufferMemory_);
        vertexCount_ = 3;
    }

    void destroyVertexBuffer() {
        if (device_.handle() == VK_NULL_HANDLE) {
            return;
        }
        if (vertexBuffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_.handle(), vertexBuffer_, nullptr);
            vertexBuffer_ = VK_NULL_HANDLE;
        }
        if (vertexBufferMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_.handle(), vertexBufferMemory_, nullptr);
            vertexBufferMemory_ = VK_NULL_HANDLE;
        }
    }

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                             ImDrawData* drawData) {
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
        const VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer_,
                               &vertexOffset);
        vkCmdDraw(commandBuffer, vertexCount_, 1, 0, 0);
        if (imguiVulkanInitialized_ && drawData != nullptr) {
            ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
        }
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
        VkExtent2D extent = window_.framebufferExtent();
        while (extent.width == 0 || extent.height == 0) {
            window_.waitEvents();
            extent = window_.framebufferExtent();
        }

        device_.waitIdle();
        swapchain_.recreate();
        pipeline_.recreate(swapchain_.imageFormat());
        commandResources_.recreate(swapchain_.imageCount());
    }

    void shutdownImGui() {
        if (imguiVulkanInitialized_) {
            ImGui_ImplVulkan_Shutdown();
            imguiVulkanInitialized_ = false;
        }
        if (imguiGlfwInitialized_) {
            ImGui_ImplGlfw_Shutdown();
            imguiGlfwInitialized_ = false;
        }
        if (imguiContextCreated_) {
            ImGui::DestroyContext();
            imguiContextCreated_ = false;
        }
    }
};

}  // namespace klib::vk
