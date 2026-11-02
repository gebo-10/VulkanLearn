#pragma once

#include <krender/IRender.h>
#include <krender/material.h>
#include <krender/mesh.h>
#include <krender/render_buffer.h>
#include <krender/resource_manager.h>
#include <krender/shader.h>
#include <krender/texture.h>
#include <krender/backend/vulkan/vulkan_context.h>

#include <kvk/buffer.h>
#include <kvk/descriptor_pool.h>
#include <kvk/descriptor_set_layout.h>
#include <kvk/device_memory.h>
#include <kvk/image.h>
#include <kvk/image_view.h>
#include <kvk/pipeline.h>
#include <kvk/pipeline_layout.h>
#include <kvk/sampler.h>
#include <kvk/shader_module.h>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace klib::krender::vulkanimpl {

class VulkanRenderImpl final : public IRender {
public:
    VulkanRenderImpl(VulkanContext& context, ResourceManager& resources)
        : context_(context),
          resources_(resources),
          defaultSampler_(std::make_shared<vk::Sampler>(
              context_.deviceOwner(), VK_FILTER_LINEAR,
              VK_SAMPLER_ADDRESS_MODE_REPEAT)) {}

    ~VulkanRenderImpl() override {
        if (context_.deviceOwner()) {
            vkDeviceWaitIdle(context_.deviceOwner()->handle());
        }
    }

    VulkanRenderImpl(const VulkanRenderImpl&) = delete;
    VulkanRenderImpl& operator=(const VulkanRenderImpl&) = delete;

    void beginFrame() override {
        if (frameActive_) {
            throw std::logic_error("A Vulkan frame is already active");
        }
        ensureDepthTargets();
        const VkResult acquireResult = context_.acquireNextImage(imageIndex_);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchainRecreationRequired_ = true;
            return;
        }
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            vk::checkVk(acquireResult, "vkAcquireNextImageKHR");
        }
        swapchainRecreationRequired_ =
            acquireResult == VK_SUBOPTIMAL_KHR;
        if (swapchainImageInitialized_.size() != context_.imageCount()) {
            swapchainImageInitialized_.assign(context_.imageCount(), false);
        }
        commandBuffer_ = context_.commandBuffer(imageIndex_);
        VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vk::checkVk(vkBeginCommandBuffer(commandBuffer_, &beginInfo),
                    "vkBeginCommandBuffer");

        VkImageMemoryBarrier toColorAttachment{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toColorAttachment.oldLayout =
            swapchainImageInitialized_.at(imageIndex_)
                ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                : VK_IMAGE_LAYOUT_UNDEFINED;
        toColorAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toColorAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachment.image =
            context_.swapchainOwner()->images().at(imageIndex_);
        toColorAttachment.subresourceRange =
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toColorAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &toColorAttachment);

        VkImageMemoryBarrier toDepthAttachment{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toDepthAttachment.oldLayout =
            depthImageInitialized_.at(imageIndex_)
                ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_UNDEFINED;
        toDepthAttachment.newLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        toDepthAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDepthAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toDepthAttachment.image = depthTargets_.at(imageIndex_).image->handle();
        toDepthAttachment.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_DEPTH_BIT |
            (hasStencilComponent(depthFormat_) ? VK_IMAGE_ASPECT_STENCIL_BIT
                                              : 0);
        toDepthAttachment.subresourceRange.levelCount = 1;
        toDepthAttachment.subresourceRange.layerCount = 1;
        toDepthAttachment.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &toDepthAttachment);

        VkRenderingAttachmentInfo colorAttachment{
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        colorAttachment.imageView =
            context_.swapchainOwner()->imageViews().at(imageIndex_);
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderingAttachmentInfo depthAttachment{
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depthAttachment.imageView = depthTargets_.at(imageIndex_).view->handle();
        depthAttachment.imageLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.clearValue.depthStencil = {1.0f, 0};

        const VkExtent2D extent = context_.extent();
        VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
        renderingInfo.renderArea = {{0, 0}, extent};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachment;
        renderingInfo.pDepthAttachment = &depthAttachment;
        if (hasStencilComponent(depthFormat_)) {
            renderingInfo.pStencilAttachment = &depthAttachment;
        }
        vkCmdBeginRendering(commandBuffer_, &renderingInfo);

        viewport_ = {0.0f, 0.0f, static_cast<float>(extent.width),
                     static_cast<float>(extent.height), 0.0f, 1.0f};
        scissor_ = {{0, 0}, extent};
        vkCmdSetViewport(commandBuffer_, 0, 1, &viewport_);
        vkCmdSetScissor(commandBuffer_, 0, 1, &scissor_);
        frameActive_ = true;
    }

    void endFrame() override {
        if (!frameActive_) {
            return;
        }
        vkCmdEndRendering(commandBuffer_);

        VkImageMemoryBarrier toPresent{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image =
            context_.swapchainOwner()->images().at(imageIndex_);
        toPresent.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(commandBuffer_,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &toPresent);
        vk::checkVk(vkEndCommandBuffer(commandBuffer_), "vkEndCommandBuffer");

        frameActive_ = false;
        const VkResult presentResult = context_.submitAndPresent(imageIndex_);
        swapchainImageInitialized_[imageIndex_] = true;
        depthImageInitialized_[imageIndex_] = true;
        swapchainRecreationRequired_ =
            swapchainRecreationRequired_ ||
            presentResult == VK_SUBOPTIMAL_KHR ||
            presentResult == VK_ERROR_OUT_OF_DATE_KHR;
        if (!retiredMeshes_.empty() || !retiredTextures_.empty() ||
            !retiredRenderBuffers_.empty() || !retiredDescriptors_.empty()) {
            context_.deviceOwner()->waitIdle();
            retiredDescriptors_.clear();
            retiredTextures_.clear();
            retiredRenderBuffers_.clear();
            retiredMeshes_.clear();
        }
        if (presentResult != VK_SUCCESS &&
            presentResult != VK_SUBOPTIMAL_KHR &&
            presentResult != VK_ERROR_OUT_OF_DATE_KHR) {
            vk::checkVk(presentResult, "vkQueuePresentKHR");
        }
    }

    void clear(ClearFlag flags, const ClearValue& value) override {
        requireActiveFrame();
        std::vector<VkClearAttachment> attachments;
        if (hasFlag(flags, ClearFlag::Color)) {
            VkClearAttachment attachment{};
            attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            attachment.colorAttachment = 0;
            attachment.clearValue.color = {{value.color.r, value.color.g,
                                            value.color.b, value.color.a}};
            attachments.push_back(attachment);
        }
        if (hasFlag(flags, ClearFlag::Depth)) {
            VkClearAttachment attachment{};
            attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            attachment.clearValue.depthStencil = {value.depth, value.stencil};
            attachments.push_back(attachment);
        }
        if (hasFlag(flags, ClearFlag::Stencil)) {
            if (!hasStencilComponent(depthFormat_)) {
                throw std::invalid_argument(
                    "The selected Vulkan depth format has no stencil component");
            }
            VkClearAttachment attachment{};
            attachment.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
            attachment.clearValue.depthStencil = {value.depth, value.stencil};
            attachments.push_back(attachment);
        }
        if (attachments.empty()) {
            return;
        }
        VkClearRect rect{};
        rect.rect = scissor_;
        rect.baseArrayLayer = 0;
        rect.layerCount = 1;
        vkCmdClearAttachments(commandBuffer_,
                              static_cast<uint32_t>(attachments.size()),
                              attachments.data(), 1, &rect);
    }

    void setViewport(const glm::vec4& viewport) override {
        requireActiveFrame();
        if (viewport.z <= 0.0f || viewport.w <= 0.0f) {
            throw std::invalid_argument(
                "Vulkan viewport width and height must be positive");
        }
        viewport_.x = viewport.x;
        viewport_.y = viewport.y;
        viewport_.width = viewport.z;
        viewport_.height = viewport.w;
        viewport_.minDepth = 0.0f;
        viewport_.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer_, 0, 1, &viewport_);
    }

    void setScissor(const glm::ivec4& scissor) override {
        requireActiveFrame();
        if (scissor.z <= 0 || scissor.w <= 0) {
            throw std::invalid_argument(
                "Vulkan scissor width and height must be positive");
        }
        scissor_.offset = {scissor.x, scissor.y};
        scissor_.extent = {static_cast<uint32_t>(scissor.z),
                           static_cast<uint32_t>(scissor.w)};
        vkCmdSetScissor(commandBuffer_, 0, 1, &scissor_);
    }

    void setViewProjection(const glm::mat4& view,
                           const glm::mat4& projection) override {
        view_ = view;
        projection_ = projection;
        viewProjection_ = projection * view;
        frameData_[0] = view_;
        frameData_[1] = projection_;
        frameData_[2] = viewProjection_;
        frameDataDirty_ = true;
    }

    void renderMesh(const Handle& meshHandle, const Handle& materialHandle,
                    int instanceCount) override {
        requireActiveFrame();
        if (instanceCount <= 0) {
            throw std::invalid_argument(
                "Mesh instance count must be greater than zero");
        }
        const auto mesh = resources_.get<Mesh>(meshHandle);
        const auto material = resources_.get<Material>(materialHandle);
        if (!mesh || !material) {
            throw std::invalid_argument(
                "renderMesh received an invalid mesh or material handle");
        }
        if (!material->shader()) {
            throw std::invalid_argument("Material has no shader");
        }
        const MeshData& data = mesh->data();
        if (data.vertexCount == 0 || data.vertexBytes == 0 ||
            data.vertices == nullptr) {
            throw std::invalid_argument("Mesh has no vertex data");
        }
        if (data.layout.stride == 0 || data.layout.attributes.empty()) {
            throw std::invalid_argument("Mesh has no vertex input layout");
        }
        if (data.vertexCount >
            std::numeric_limits<VkDeviceSize>::max() / data.layout.stride ||
            data.vertexBytes <
                static_cast<VkDeviceSize>(data.vertexCount) *
                    data.layout.stride) {
            throw std::invalid_argument(
                "Mesh vertex data is smaller than its declared layout");
        }
        if (data.indexCount != 0) {
            VkDeviceSize indexSize = 0;
            if (data.indexType == VK_INDEX_TYPE_UINT16) {
                indexSize = sizeof(uint16_t);
            } else if (data.indexType == VK_INDEX_TYPE_UINT32) {
                indexSize = sizeof(uint32_t);
            } else {
                throw std::invalid_argument(
                    "VulkanRenderImpl supports 16-bit and 32-bit indices");
            }
            if (data.indexCount >
                    std::numeric_limits<VkDeviceSize>::max() / indexSize ||
                data.indexBytes <
                    static_cast<VkDeviceSize>(data.indexCount) * indexSize) {
                throw std::invalid_argument(
                    "Mesh index data is smaller than its declared count");
            }
        }

        syncFrameDataBuffer();
        const GpuMesh& gpuMesh = resolveMesh(*mesh);
        const auto pipeline = resolvePipeline(*material->shader(), data.layout);
        const auto descriptorSets =
            resolveDescriptorSets(*material, *material->shader(), *pipeline);

        vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline->pipeline->handle());
        if (!descriptorSets.empty()) {
            vkCmdBindDescriptorSets(
                commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipeline->layout->handle(), 0,
                static_cast<uint32_t>(descriptorSets.size()),
                descriptorSets.data(), 0, nullptr);
        }
        pushConstants(*material, *material->shader(), *pipeline);

        const VkDeviceSize vertexOffset = 0;
        const VkBuffer vertexBuffer = gpuMesh.vertex.buffer->handle();
        vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &vertexBuffer,
                               &vertexOffset);
        if (gpuMesh.index.buffer) {
            vkCmdBindIndexBuffer(commandBuffer_, gpuMesh.index.buffer->handle(),
                                 0, data.indexType);
            vkCmdDrawIndexed(commandBuffer_, data.indexCount,
                             static_cast<uint32_t>(instanceCount), 0, 0, 0);
        } else {
            vkCmdDraw(commandBuffer_, data.vertexCount,
                      static_cast<uint32_t>(instanceCount), 0, 0);
        }
    }

    void recreateSwapchain(VkExtent2D framebufferExtent) {
        if (frameActive_) {
            throw std::logic_error(
                "Cannot recreate the swapchain while a frame is active");
        }
        context_.recreateSwapchain(framebufferExtent);
        swapchainRecreationRequired_ = false;
        depthTargets_.clear();
        depthExtent_ = {};
        depthImageInitialized_.clear();
        swapchainImageInitialized_.assign(context_.imageCount(), false);
    }

    bool swapchainRecreationRequired() const noexcept {
        return swapchainRecreationRequired_;
    }

private:
    struct BufferAllocation {
        vk::DeviceMemoryPtr memory;
        vk::BufferPtr buffer;
    };

    struct GpuMesh {
        uint32_t version = UINT32_MAX;
        uint32_t sizeEpoch = UINT32_MAX;
        BufferAllocation vertex;
        BufferAllocation index;
    };

    struct GpuTexture {
        uint32_t version = UINT32_MAX;
        vk::DeviceMemoryPtr memory;
        vk::ImagePtr image;
        vk::ImageViewPtr view;
        vk::SamplerPtr sampler;
    };

    struct GpuRenderBuffer {
        uint32_t version = UINT32_MAX;
        BufferAllocation allocation;
    };

    struct DepthTarget {
        vk::DeviceMemoryPtr memory;
        vk::ImagePtr image;
        vk::ImageViewPtr view;
    };

    struct PipelineState {
        std::vector<vk::DescriptorSetLayoutPtr> setLayouts;
        std::vector<VkPushConstantRange> pushRanges;
        vk::PipelineLayoutPtr layout;
        vk::PipelinePtr pipeline;
    };

    struct DescriptorState {
        uint64_t fingerprint = 0;
        uint64_t shaderFingerprint = 0;
        vk::DescriptorPoolPtr pool;
        std::vector<VkDescriptorSet> sets;
    };

    VulkanContext& context_;
    ResourceManager& resources_;
    vk::SamplerPtr defaultSampler_;
    std::unordered_map<Handle, GpuMesh, HandleHash> meshes_;
    std::unordered_map<Handle, GpuTexture, HandleHash> textures_;
    std::unordered_map<Handle, GpuRenderBuffer, HandleHash> renderBuffers_;
    std::unordered_map<std::string, std::shared_ptr<PipelineState>> pipelines_;
    std::unordered_map<Handle, DescriptorState, HandleHash> descriptors_;
    std::vector<GpuMesh> retiredMeshes_;
    std::vector<GpuTexture> retiredTextures_;
    std::vector<GpuRenderBuffer> retiredRenderBuffers_;
    std::vector<DescriptorState> retiredDescriptors_;
    std::vector<DepthTarget> depthTargets_;
    std::vector<bool> depthImageInitialized_;
    std::vector<bool> swapchainImageInitialized_;
    std::array<glm::mat4, 3> frameData_{
        glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
    BufferAllocation frameDataBuffer_;
    VkExtent2D depthExtent_{};
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    uint32_t imageIndex_ = 0;
    VkViewport viewport_{};
    VkRect2D scissor_{};
    glm::mat4 view_{1.0f};
    glm::mat4 projection_{1.0f};
    glm::mat4 viewProjection_{1.0f};
    bool frameActive_ = false;
    bool frameDataDirty_ = true;
    bool swapchainRecreationRequired_ = false;

    static constexpr const char* FrameDataBindingName = "frameData";

    void requireActiveFrame() const {
        if (!frameActive_) {
            throw std::logic_error(
                "Rendering commands require an active Vulkan frame");
        }
    }

    BufferAllocation createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  const void* data) {
        if (size == 0 || data == nullptr) {
            throw std::invalid_argument(
                "GPU buffer creation requires non-empty source data");
        }
        BufferAllocation allocation;
        allocation.buffer =
            std::make_shared<vk::Buffer>(
                context_.deviceOwner(), size,
                usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        const VkMemoryRequirements requirements =
            allocation.buffer->memoryRequirements();
        allocation.memory = std::make_shared<vk::DeviceMemory>(
            context_.deviceOwner(), requirements.size,
            context_.findMemoryType(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
        allocation.buffer->bindMemory(allocation.memory->handle());
        context_.uploadToBuffer(allocation.buffer->handle(), 0, size, data);
        return allocation;
    }

    GpuMesh& resolveMesh(const Mesh& mesh) {
        const Handle handle = mesh.handle();
        GpuMesh& cached = meshes_[handle];
        if (cached.version == mesh.version() &&
            cached.sizeEpoch == mesh.sizeEpoch()) {
            return cached;
        }
        const MeshData& data = mesh.data();
        GpuMesh replacement;
        replacement.version = mesh.version();
        replacement.sizeEpoch = mesh.sizeEpoch();
        replacement.vertex = createBuffer(
            data.vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, data.vertices);
        if (data.indexCount != 0) {
            if (data.indexBytes == 0 || data.indices == nullptr) {
                throw std::invalid_argument(
                    "Indexed mesh has no index data");
            }
            replacement.index = createBuffer(
                data.indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                data.indices);
        }
        if (cached.vertex.buffer || cached.index.buffer) {
            retiredMeshes_.push_back(cached);
        }
        cached = std::move(replacement);
        return cached;
    }

    void syncFrameDataBuffer() {
        if (!frameDataDirty_) {
            return;
        }
        const VkDeviceSize size = sizeof(frameData_);
        if (!frameDataBuffer_.buffer) {
            frameDataBuffer_ = createBuffer(
                size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, frameData_.data());
        } else {
            context_.uploadToBuffer(frameDataBuffer_.buffer->handle(), 0, size,
                                    frameData_.data());
        }
        frameDataDirty_ = false;
    }

    GpuRenderBuffer& resolveRenderBuffer(const RenderBuffer& renderBuffer) {
        GpuRenderBuffer& cached = renderBuffers_[renderBuffer.handle()];
        if (cached.allocation.buffer &&
            cached.version == renderBuffer.version()) {
            return cached;
        }

        GpuRenderBuffer replacement;
        replacement.version = renderBuffer.version();
        replacement.allocation = createBuffer(
            renderBuffer.size(), renderBuffer.usage(), renderBuffer.data());
        if (cached.allocation.buffer) {
            retiredRenderBuffers_.push_back(cached);
        }
        cached = std::move(replacement);
        return cached;
    }

    static VkShaderStageFlags toVkStages(ShaderStage stages) {
        VkShaderStageFlags result = 0;
        if (hasShaderStage(stages, ShaderStage::Vertex)) {
            result |= VK_SHADER_STAGE_VERTEX_BIT;
        }
        if (hasShaderStage(stages, ShaderStage::Fragment)) {
            result |= VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        if (hasShaderStage(stages, ShaderStage::Geometry)) {
            result |= VK_SHADER_STAGE_GEOMETRY_BIT;
        }
        if (hasShaderStage(stages, ShaderStage::TessControl)) {
            result |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        }
        if (hasShaderStage(stages, ShaderStage::TessEval)) {
            result |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        }
        return result;
    }

    static VkDescriptorType toVkDescriptorType(BindingType type) {
        switch (type) {
            case BindingType::UniformBuffer:
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case BindingType::StorageBuffer:
                return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case BindingType::SampledImage:
                return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case BindingType::Sampler:
                return VK_DESCRIPTOR_TYPE_SAMPLER;
            case BindingType::CombinedImageSampler:
                return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            default:
                throw std::invalid_argument(
                    "Material does not provide data for this shader descriptor type");
        }
    }

    void ensureDepthTargets() {
            const VkExtent2D extent = context_.extent();
            if (depthTargets_.size() == context_.imageCount() &&
                depthExtent_.width == extent.width &&
                depthExtent_.height == extent.height) {
                return;
            }

            context_.deviceOwner()->waitIdle();
            if (depthFormat_ == VK_FORMAT_UNDEFINED) {
                constexpr VkFormat candidates[] = {
                    VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT,
                    VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM};
                for (VkFormat candidate : candidates) {
                    VkFormatProperties properties{};
                    vkGetPhysicalDeviceFormatProperties(
                        context_.deviceOwner()->physicalDevice(), candidate,
                        &properties);
                    if ((properties.optimalTilingFeatures &
                         VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
                        depthFormat_ = candidate;
                        break;
                    }
                }
                if (depthFormat_ == VK_FORMAT_UNDEFINED) {
                    throw std::runtime_error(
                        "The Vulkan device has no supported depth attachment format");
                }
            }

            depthTargets_.clear();
            depthTargets_.resize(context_.imageCount());
            depthImageInitialized_.assign(context_.imageCount(), false);
            const VkImageAspectFlags aspectMask =
                VK_IMAGE_ASPECT_DEPTH_BIT |
                (hasStencilComponent(depthFormat_) ? VK_IMAGE_ASPECT_STENCIL_BIT
                                                  : 0);
            for (DepthTarget& target : depthTargets_) {
                target.image = std::make_shared<vk::Image>(
                    context_.deviceOwner(), VK_IMAGE_TYPE_2D, depthFormat_,
                    VkExtent3D{extent.width, extent.height, 1},
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
                const VkMemoryRequirements requirements =
                    target.image->memoryRequirements();
                target.memory = std::make_shared<vk::DeviceMemory>(
                    context_.deviceOwner(), requirements.size,
                    context_.findMemoryType(
                        requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
                target.image->bindMemory(target.memory->handle());
                target.view = std::make_shared<vk::ImageView>(
                    context_.deviceOwner(), target.image->handle(), depthFormat_,
                    aspectMask);
            }
            depthExtent_ = extent;
        }

    static bool hasStencilComponent(VkFormat format) {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
               format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    static std::string pipelineKey(const Shader& shader,
                                  const VertexLayout& layout,
                                  VkFormat colorFormat) {
        std::string key = std::to_string(shader.handle().index) + ":" +
                          std::to_string(shader.handle().generation) + ":" +
                          std::to_string(shader.interface().fingerprint()) +
                          ":" + std::to_string(colorFormat) + ":" +
                          std::to_string(layout.stride);
        for (const VertexAttribute& attribute : layout.attributes) {
            key += ":" + std::to_string(attribute.location) + "," +
                   std::to_string(attribute.format) + "," +
                   std::to_string(attribute.offset);
        }
        return key;
    }

    std::shared_ptr<PipelineState> resolvePipeline(
        const Shader& shader, const VertexLayout& vertexLayout) {
        std::string error;
        if (!shader.validate(&error)) {
            throw std::invalid_argument("Invalid shader: " + error);
        }
        if (shader.moduleFor(ShaderStage::Compute) != nullptr) {
            throw std::invalid_argument(
                "Compute shaders cannot be used by renderMesh");
        }
        for (const ShaderModule& module : shader.stages()) {
            if (module.stage() != ShaderStage::Vertex &&
                module.stage() != ShaderStage::Fragment) {
                throw std::invalid_argument(
                    "VulkanRenderImpl currently supports vertex/fragment shader stages");
            }
        }
        if (shader.moduleFor(ShaderStage::Fragment) == nullptr) {
            throw std::invalid_argument(
                "Graphics shader requires a fragment stage");
        }

        const VkFormat colorFormat = context_.swapchainOwner()->imageFormat();
        const std::string key =
            pipelineKey(shader, vertexLayout, colorFormat) + ":" +
            std::to_string(depthFormat_);
        const auto found = pipelines_.find(key);
        if (found != pipelines_.end()) {
            return found->second;
        }

        auto state = std::make_shared<PipelineState>();
        const ShaderInterface& shaderInterface = shader.interface();
        uint32_t setCount = 0;
        for (const ShaderBinding& binding : shaderInterface.bindings) {
            if (binding.set == UINT32_MAX) {
                throw std::invalid_argument(
                    "Shader descriptor set index is out of range");
            }
            setCount = std::max(setCount, binding.set + 1);
        }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(context_.deviceOwner()->physicalDevice(),
                                      &properties);
        if (setCount > properties.limits.maxBoundDescriptorSets) {
            throw std::invalid_argument(
                "Shader uses more descriptor sets than this device supports");
        }

        std::vector<std::vector<VkDescriptorSetLayoutBinding>> layoutBindings(
            setCount);
        for (const ShaderBinding& binding : shaderInterface.bindings) {
            if (binding.count != 1) {
                throw std::invalid_argument(
                    "Material currently supports one resource per shader binding");
            }
            VkDescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = binding.binding;
            layoutBinding.descriptorType = toVkDescriptorType(binding.type);
            layoutBinding.descriptorCount = binding.count;
            layoutBinding.stageFlags = toVkStages(binding.stages);
            layoutBindings[binding.set].push_back(layoutBinding);
        }
        std::vector<VkDescriptorSetLayout> rawLayouts;
        rawLayouts.reserve(setCount);
        for (const auto& bindings : layoutBindings) {
            auto setLayout = std::make_shared<vk::DescriptorSetLayout>(
                context_.deviceOwner(), bindings);
            rawLayouts.push_back(setLayout->handle());
            state->setLayouts.push_back(std::move(setLayout));
        }

        for (const PushConstantRange& reflected :
             shaderInterface.pushConstantRanges) {
            if (reflected.offset > properties.limits.maxPushConstantsSize ||
                reflected.size >
                    properties.limits.maxPushConstantsSize - reflected.offset) {
                throw std::invalid_argument(
                    "Shader push-constant range exceeds the device limit");
            }
            VkPushConstantRange range{};
            range.stageFlags = toVkStages(reflected.stages);
            range.offset = reflected.offset;
            range.size = reflected.size;
            state->pushRanges.push_back(range);
        }
        state->layout = std::make_shared<vk::PipelineLayout>(
            context_.deviceOwner(), rawLayouts, state->pushRanges);

        std::vector<vk::ShaderModulePtr> shaderModules;
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        for (const ShaderModule& module : shader.stages()) {
            std::vector<uint32_t> words(module.spirv().size() / sizeof(uint32_t));
            std::memcpy(words.data(), module.spirv().data(),
                        module.spirv().size());
            VkShaderModuleCreateInfo moduleInfo{
                VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            moduleInfo.codeSize = module.spirv().size();
            moduleInfo.pCode = words.data();
            auto shaderModule = std::make_shared<vk::ShaderModule>(
                context_.deviceOwner(), moduleInfo);
            VkPipelineShaderStageCreateInfo stageInfo{
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            stageInfo.stage = static_cast<VkShaderStageFlagBits>(
                toVkStages(module.stage()));
            stageInfo.module = shaderModule->handle();
            stageInfo.pName = module.entryPoint().c_str();
            shaderStages.push_back(stageInfo);
            shaderModules.push_back(std::move(shaderModule));
        }

        VkVertexInputBindingDescription vertexBinding{};
        vertexBinding.binding = 0;
        vertexBinding.stride = vertexLayout.stride;
        vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        std::vector<VkVertexInputAttributeDescription> attributes;
        attributes.reserve(vertexLayout.attributes.size());
        for (const VertexAttribute& attribute : vertexLayout.attributes) {
            attributes.push_back({attribute.location, 0, attribute.format,
                                  attribute.offset});
        }
        VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &vertexBinding;
        vertexInput.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewportState{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                                VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;
        VkPipelineRasterizationStateCreateInfo rasterization{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterization.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisampling{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depthStencil{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.stencilTestEnable = VK_FALSE;
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blending{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blending.attachmentCount = 1;
        blending.pAttachments = &blendAttachment;
        VkPipelineRenderingCreateInfo rendering{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &colorFormat;
        rendering.depthAttachmentFormat = depthFormat_;
        rendering.stencilAttachmentFormat =
            hasStencilComponent(depthFormat_) ? depthFormat_
                                               : VK_FORMAT_UNDEFINED;
        VkGraphicsPipelineCreateInfo pipelineInfo{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.pNext = &rendering;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterization;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &blending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = state->layout->handle();
        state->pipeline =
            std::make_shared<vk::Pipeline>(context_.deviceOwner(), pipelineInfo);
        pipelines_.emplace(key, state);
        return state;
    }

    uint64_t descriptorFingerprint(const Material& material) const {
        uint64_t hash = material.version();
        for (const auto& [name, texture] : material.textures()) {
            for (unsigned char ch : name) {
                hash = (hash ^ ch) * 1099511628211ull;
            }
            hash = (hash ^ texture->handle().index) * 1099511628211ull;
            hash = (hash ^ texture->handle().generation) * 1099511628211ull;
            hash = (hash ^ texture->version()) * 1099511628211ull;
        }
        for (const auto& [name, renderBuffer] : material.renderBuffers()) {
            for (unsigned char ch : name) {
                hash = (hash ^ ch) * 1099511628211ull;
            }
            hash = (hash ^ renderBuffer->handle().index) * 1099511628211ull;
            hash =
                (hash ^ renderBuffer->handle().generation) * 1099511628211ull;
            hash = (hash ^ renderBuffer->version()) * 1099511628211ull;
        }
        return hash;
    }

    std::vector<VkDescriptorSet> resolveDescriptorSets(
        const Material& material, const Shader& shader,
        const PipelineState& pipeline) {
        const uint64_t fingerprint = descriptorFingerprint(material);
        const uint64_t shaderFingerprint = shader.interface().fingerprint();
        DescriptorState& cached = descriptors_[material.handle()];
        if (cached.pool && cached.fingerprint == fingerprint &&
            cached.shaderFingerprint == shaderFingerprint) {
            return cached.sets;
        }

        if (cached.pool) {
            retiredDescriptors_.push_back(cached);
        }
        cached = {};
        if (pipeline.setLayouts.empty()) {
            cached.fingerprint = fingerprint;
            cached.shaderFingerprint = shaderFingerprint;
            return {};
        }

        std::vector<VkDescriptorPoolSize> poolSizes;
        std::unordered_map<VkDescriptorType, uint32_t> descriptorCounts;
        for (const ShaderBinding& binding : shader.interface().bindings) {
            descriptorCounts[toVkDescriptorType(binding.type)] += binding.count;
        }
        for (const auto& [type, count] : descriptorCounts) {
            poolSizes.push_back({type, count});
        }
        cached.pool = std::make_shared<vk::DescriptorPool>(
            context_.deviceOwner(),
            static_cast<uint32_t>(pipeline.setLayouts.size()), poolSizes);
        std::vector<VkDescriptorSetLayout> layouts;
        layouts.reserve(pipeline.setLayouts.size());
        for (const auto& layout : pipeline.setLayouts) {
            layouts.push_back(layout->handle());
        }
        cached.sets = cached.pool->allocate(layouts);

        std::vector<VkDescriptorImageInfo> imageInfos;
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkWriteDescriptorSet> writes;
        imageInfos.reserve(shader.interface().bindings.size());
        bufferInfos.reserve(shader.interface().bindings.size());
        writes.reserve(shader.interface().bindings.size());
        for (const ShaderBinding& binding : shader.interface().bindings) {
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = cached.sets.at(binding.set);
            write.dstBinding = binding.binding;
            write.descriptorCount = binding.count;
            write.descriptorType = toVkDescriptorType(binding.type);
            if (binding.type == BindingType::UniformBuffer ||
                binding.type == BindingType::StorageBuffer) {
                if (binding.type == BindingType::UniformBuffer &&
                    binding.name == FrameDataBindingName) {
                    if (!frameDataBuffer_.buffer) {
                        throw std::logic_error(
                            "Frame data buffer has not been initialized");
                    }
                    bufferInfos.push_back(
                        {frameDataBuffer_.buffer->handle(), 0,
                         sizeof(frameData_)});
                    write.pBufferInfo = &bufferInfos.back();
                    writes.push_back(write);
                    continue;
                }
                const auto* renderBuffer =
                    material.renderBuffer(binding.name);
                if (renderBuffer == nullptr || !*renderBuffer) {
                    throw std::invalid_argument(
                        "Material is missing render buffer binding: " +
                        binding.name);
                }
                const VkBufferUsageFlags requiredUsage =
                    binding.type == BindingType::UniformBuffer
                        ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
                        : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                if (((*renderBuffer)->usage() & requiredUsage) == 0) {
                    throw std::invalid_argument(
                        "Render buffer usage does not match shader binding: " +
                        binding.name);
                }
                const GpuRenderBuffer& gpuBuffer =
                    resolveRenderBuffer(**renderBuffer);
                bufferInfos.push_back(
                    {gpuBuffer.allocation.buffer->handle(), 0,
                     (*renderBuffer)->size()});
                write.pBufferInfo = &bufferInfos.back();
            } else if (binding.type == BindingType::SampledImage ||
                       binding.type == BindingType::CombinedImageSampler) {
                VkDescriptorImageInfo imageInfo{};
                const auto* texture = material.texture(binding.name);
                if (texture == nullptr || !*texture) {
                    throw std::invalid_argument(
                        "Material is missing texture binding: " + binding.name);
                }
                const GpuTexture& gpuTexture = resolveTexture(**texture);
                imageInfo.imageView = gpuTexture.view->handle();
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (binding.type == BindingType::CombinedImageSampler) {
                    imageInfo.sampler = gpuTexture.sampler->handle();
                }
                imageInfos.push_back(imageInfo);
                write.pImageInfo = &imageInfos.back();
            } else if (binding.type == BindingType::Sampler) {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.sampler = defaultSampler_->handle();
                imageInfos.push_back(imageInfo);
                write.pImageInfo = &imageInfos.back();
            } else {
                throw std::invalid_argument(
                    "Material does not support this shader descriptor type");
            }
            writes.push_back(write);
        }
        cached.pool->update(static_cast<uint32_t>(writes.size()), writes.data());
        cached.fingerprint = fingerprint;
        cached.shaderFingerprint = shaderFingerprint;
        return cached.sets;
    }

    GpuTexture& resolveTexture(const Texture& texture) {
        GpuTexture& cached = textures_[texture.handle()];
        if (cached.image && cached.version == texture.version()) {
            return cached;
        }
        GpuTexture replacement;
        replacement.version = texture.version();
        replacement.image = std::make_shared<vk::Image>(
            context_.deviceOwner(), VK_IMAGE_TYPE_2D,
            VK_FORMAT_R8G8B8A8_UNORM,
            VkExtent3D{texture.width(), texture.height(), 1},
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        const VkMemoryRequirements requirements =
            replacement.image->memoryRequirements();
        replacement.memory = std::make_shared<vk::DeviceMemory>(
            context_.deviceOwner(), requirements.size,
            context_.findMemoryType(requirements.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
        replacement.image->bindMemory(replacement.memory->handle());
        replacement.view = std::make_shared<vk::ImageView>(
            context_.deviceOwner(), replacement.image->handle(),
            VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
        replacement.sampler = std::make_shared<vk::Sampler>(
            context_.deviceOwner(), VK_FILTER_LINEAR,
            VK_SAMPLER_ADDRESS_MODE_REPEAT);
        context_.uploadToImage(replacement.image->handle(), texture.width(),
                               texture.height(), texture.pixels(),
                               texture.bytesPerRow());
        if (cached.image) {
            retiredTextures_.push_back(cached);
        }
        cached = std::move(replacement);
        return cached;
    }

    void pushConstants(const Material& material, const Shader& shader,
                       const PipelineState& pipeline) {
        const std::vector<std::byte>& materialBytes =
            material.propertyData();
        const uint32_t requiredSize = shader.interface().pushConstantSize();
        if (materialBytes.size() < requiredSize) {
            throw std::invalid_argument(
                "Material push-constant storage does not match its shader layout");
        }

        for (const PushConstantRange& range :
             shader.interface().pushConstantRanges) {
            if (range.offset > materialBytes.size() ||
                range.size > materialBytes.size() - range.offset) {
                throw std::out_of_range(
                    "Push-constant range exceeds material storage");
            }
            vkCmdPushConstants(
                commandBuffer_, pipeline.layout->handle(),
                toVkStages(range.stages), range.offset, range.size,
                materialBytes.data() + range.offset);
        }
    }
};

}  // namespace klib::krender::vulkanimpl
