#pragma once

#include "renderer_config.h"
#include <kvk/command_resources.h>
#include <kvk/graphics_pipeline.h>
#include <kvk/swapchain.h>
#include <kvk/sync_objects.h>
#include <kvk/window.h>
#include <kvk/transient_command_pool.h>


#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <stb_image.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

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
        pipeline_.shutdown();
        destroyTextureResources();
        destroyVertexBuffer();
    }

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void initialize() {
        instance_.initialize(window_);
        device_.initialize(instance_);
        swapchain_.initialize(device_, instance_.surface(), window_);

        transients_.initialize(device_.handle(), device_.graphicsQueueFamily());

        createVertexBuffer();
        createTextureImage();
        createTextureSampler();
        createTextureDescriptor();
        pipeline_.initialize(device_.handle(), swapchain_.imageFormat(),
                             PipelineConfig::triangle(
                                 textureDescriptorSetLayout_));
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

    void setTriangleColor(float red, float green, float blue) {
        triangleColor_ = {red, green, blue, 1.0f};
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
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory_ = VK_NULL_HANDLE;
    uint32_t indexCount_ = 0;
    TransientCommandPool transients_;
    VkImage textureImage_ = VK_NULL_HANDLE;
    VkDeviceMemory textureImageMemory_ = VK_NULL_HANDLE;
    VkImageView textureImageView_ = VK_NULL_HANDLE;
    VkSampler textureSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout textureDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool textureDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet textureDescriptorSet_ = VK_NULL_HANDLE;
    std::array<float, 4> triangleColor_{1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t currentFrame_ = 0;
    bool framebufferResized_ = false;
    bool imguiContextCreated_ = false;
    bool imguiGlfwInitialized_ = false;
    bool imguiVulkanInitialized_ = false;

    void createVertexBuffer() {
        constexpr Vertex vertices[] = {
            {{-1.0f, -1.0f}, {1.0f, 0.2f, 0.2f}, {0.0f, 0.0f}},
            {{1.0f, -1.0f}, {0.2f, 1.0f, 0.3f}, {1.0f, 0.0f}},
            {{1.0f, 1.0f}, {0.2f, 0.4f, 1.0f}, {1.0f, 1.0f}},
            {{-1.0f, 1.0f}, {1.0f, 1.0f, 0.2f}, {0.0f, 1.0f}},
        };
        constexpr uint16_t indices[] = {0, 1, 2, 0, 2, 3};

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

        createBuffer(sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     indexBuffer_, indexBufferMemory_);
        checkVk(vkMapMemory(device_.handle(), indexBufferMemory_, 0,
                            sizeof(indices), 0, &mappedMemory),
                "vkMapMemory");
        std::memcpy(mappedMemory, indices, sizeof(indices));
        vkUnmapMemory(device_.handle(), indexBufferMemory_);
        indexCount_ = 6;
    }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags memoryProperties, VkBuffer& buffer,
                      VkDeviceMemory& memory) {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        checkVk(vkCreateBuffer(device_.handle(), &bufferInfo, nullptr, &buffer),
                "vkCreateBuffer");

        try {
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device_.handle(), buffer,
                                          &requirements);
            VkMemoryAllocateInfo allocateInfo{
                VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocateInfo.allocationSize = requirements.size;
            allocateInfo.memoryTypeIndex = device_.findMemoryType(
                requirements.memoryTypeBits, memoryProperties);
            checkVk(vkAllocateMemory(device_.handle(), &allocateInfo, nullptr,
                                     &memory),
                    "vkAllocateMemory");
            checkVk(vkBindBufferMemory(device_.handle(), buffer, memory, 0),
                    "vkBindBufferMemory");
        } catch (...) {
            if (memory != VK_NULL_HANDLE) {
                vkFreeMemory(device_.handle(), memory, nullptr);
                memory = VK_NULL_HANDLE;
            }
            vkDestroyBuffer(device_.handle(), buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            throw;
        }
    }

    void createTextureImage() {
        std::filesystem::path texturePath =
            std::filesystem::path("assets") / "images" / "coffic.png";
        if (!std::filesystem::exists(texturePath)) {
            const auto executableRelativePath =
                executableDirectory().parent_path() / texturePath;
            if (std::filesystem::exists(executableRelativePath)) {
                texturePath = executableRelativePath;
            }
        }
        const std::string texturePathString = texturePath.string();
        int width = 0;
        int height = 0;
        int sourceChannels = 0;
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
            stbi_load(texturePathString.c_str(), &width, &height, &sourceChannels,
                      STBI_rgb_alpha),
            stbi_image_free);
        if (!pixels) {
            const char* reason = stbi_failure_reason();
            throw std::runtime_error(
                std::string("Failed to load texture ") + texturePathString + ": " +
                (reason != nullptr ? reason : "unknown stb_image error"));
        }
        if (width <= 0 || height <= 0) {
            throw std::runtime_error(std::string("Invalid texture dimensions: ") +
                                     texturePathString);
        }
        if (sourceChannels < 1 || sourceChannels > 4) {
            throw std::runtime_error(std::string("Invalid texture channel count: ") +
                                     texturePathString);
        }

        const VkDeviceSize imageSize =
            static_cast<VkDeviceSize>(width) *
            static_cast<VkDeviceSize>(height) * 4;
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingMemory);
        try {
            void* mappedMemory = nullptr;
            checkVk(vkMapMemory(device_.handle(), stagingMemory, 0, imageSize, 0,
                                &mappedMemory),
                    "vkMapMemory");
            std::memcpy(mappedMemory, pixels.get(),
                        static_cast<size_t>(imageSize));
            vkUnmapMemory(device_.handle(), stagingMemory);

            VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent = {static_cast<uint32_t>(width),
                                static_cast<uint32_t>(height), 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            checkVk(vkCreateImage(device_.handle(), &imageInfo, nullptr,
                                  &textureImage_),
                    "vkCreateImage");

            VkMemoryRequirements imageRequirements{};
            vkGetImageMemoryRequirements(device_.handle(), textureImage_,
                                         &imageRequirements);
            VkMemoryAllocateInfo allocateInfo{
                VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocateInfo.allocationSize = imageRequirements.size;
            allocateInfo.memoryTypeIndex = device_.findMemoryType(
                imageRequirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            checkVk(vkAllocateMemory(device_.handle(), &allocateInfo, nullptr,
                                     &textureImageMemory_),
                    "vkAllocateMemory");
            checkVk(vkBindImageMemory(device_.handle(), textureImage_,
                                      textureImageMemory_, 0),
                    "vkBindImageMemory");

            uploadTexture(stagingBuffer, static_cast<uint32_t>(width),
                          static_cast<uint32_t>(height));

            VkImageViewCreateInfo viewInfo{
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = textureImage_;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
            viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                                   VK_COMPONENT_SWIZZLE_IDENTITY,
                                   VK_COMPONENT_SWIZZLE_IDENTITY,
                                   VK_COMPONENT_SWIZZLE_IDENTITY};
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            checkVk(vkCreateImageView(device_.handle(), &viewInfo, nullptr,
                                      &textureImageView_),
                    "vkCreateImageView");
        } catch (...) {
            vkDestroyBuffer(device_.handle(), stagingBuffer, nullptr);
            vkFreeMemory(device_.handle(), stagingMemory, nullptr);
            throw;
        }
        vkDestroyBuffer(device_.handle(), stagingBuffer, nullptr);
        vkFreeMemory(device_.handle(), stagingMemory, nullptr);
    }

    void uploadTexture(VkBuffer stagingBuffer, uint32_t width, uint32_t height) {
        transients_.oneShot(device_.graphicsQueue(), [&](VkCommandBuffer cmd) {
            // ① barrier: UNDEFINED → TRANSFER_DST_OPTIMAL
            VkImageMemoryBarrier toTransfer{
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toTransfer.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
            toTransfer.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransfer.srcAccessMask = 0;
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toTransfer.image         = textureImage_;
            toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &toTransfer);

            // ② copy
            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {width, height, 1};
            vkCmdCopyBufferToImage(cmd, stagingBuffer, textureImage_,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            // ③ barrier: TRANSFER_DST → SHADER_READ_ONLY
            VkImageMemoryBarrier toShaderRead{
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            toShaderRead.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toShaderRead.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toShaderRead.image         = textureImage_;
            toShaderRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &toShaderRead);
        });
    }

    void createTextureSampler() {
        VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        checkVk(vkCreateSampler(device_.handle(), &samplerInfo, nullptr,
                                &textureSampler_),
                "vkCreateSampler");
    }

    void createTextureDescriptor() {
        VkDescriptorSetLayoutBinding bindings[2]{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings;
        checkVk(vkCreateDescriptorSetLayout(
                    device_.handle(), &layoutInfo, nullptr,
                    &textureDescriptorSetLayout_),
                "vkCreateDescriptorSetLayout");

        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
            {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};
        VkDescriptorPoolCreateInfo poolInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        checkVk(vkCreateDescriptorPool(device_.handle(), &poolInfo, nullptr,
                                       &textureDescriptorPool_),
                "vkCreateDescriptorPool");

        VkDescriptorSetAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocateInfo.descriptorPool = textureDescriptorPool_;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &textureDescriptorSetLayout_;
        checkVk(vkAllocateDescriptorSets(device_.handle(), &allocateInfo,
                                         &textureDescriptorSet_),
                "vkAllocateDescriptorSets");

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = textureImageView_;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo samplerInfo{};
        samplerInfo.sampler = textureSampler_;
        VkWriteDescriptorSet writes[2]{};
        for (VkWriteDescriptorSet& write : writes) {
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = textureDescriptorSet_;
            write.descriptorCount = 1;
        }
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[0].pImageInfo = &imageInfo;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        writes[1].pImageInfo = &samplerInfo;
        vkUpdateDescriptorSets(device_.handle(), 2, writes, 0, nullptr);
    }

    void destroyTextureResources() {
        if (device_.handle() == VK_NULL_HANDLE) {
            return;
        }
        if (textureDescriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_.handle(), textureDescriptorPool_,
                                    nullptr);
            textureDescriptorPool_ = VK_NULL_HANDLE;
            textureDescriptorSet_ = VK_NULL_HANDLE;
        }
        if (textureSampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_.handle(), textureSampler_, nullptr);
            textureSampler_ = VK_NULL_HANDLE;
        }
        if (textureImageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_.handle(), textureImageView_, nullptr);
            textureImageView_ = VK_NULL_HANDLE;
        }
        if (textureImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_.handle(), textureImage_, nullptr);
            textureImage_ = VK_NULL_HANDLE;
        }
        if (textureImageMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_.handle(), textureImageMemory_, nullptr);
            textureImageMemory_ = VK_NULL_HANDLE;
        }
        if (textureDescriptorSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_.handle(),
                                         textureDescriptorSetLayout_, nullptr);
            textureDescriptorSetLayout_ = VK_NULL_HANDLE;
        }
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
        if (indexBuffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_.handle(), indexBuffer_, nullptr);
            indexBuffer_ = VK_NULL_HANDLE;
        }
        if (indexBufferMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_.handle(), indexBufferMemory_, nullptr);
            indexBufferMemory_ = VK_NULL_HANDLE;
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
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline_.layout(), 0, 1,
                                &textureDescriptorSet_, 0, nullptr);
        vkCmdPushConstants(commandBuffer, pipeline_.layout(),
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(triangleColor_), triangleColor_.data());

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
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer_, 0,
                             VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(commandBuffer, indexCount_, 1, 0, 0, 0);
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
