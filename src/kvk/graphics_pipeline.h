#pragma once

#include <kvk/pipeline.h>
#include <kvk/pipeline_layout.h>
#include <kvk/shader_module.h>

#include <cstddef>
#include <filesystem>
#include <vector>

namespace klib::vk {

struct Vertex {
    float position[2];
    float color[3];
    float uv[2];
};

struct PipelineConfig {
    std::filesystem::path vertexShader;
    std::filesystem::path fragmentShader;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;

    static PipelineConfig triangle(VkDescriptorSetLayout textureLayout) {
        const auto shaderDirectory = executableDirectory() / L"shaders";
        PipelineConfig config;
        config.vertexShader = shaderDirectory / L"triangle.vert.spv";
        config.fragmentShader = shaderDirectory / L"triangle.frag.spv";
        config.vertexBindings.push_back(
            {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX});
        config.vertexAttributes = {
            {0, 0, VK_FORMAT_R32G32_SFLOAT,
             static_cast<uint32_t>(offsetof(Vertex, position))},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT,
             static_cast<uint32_t>(offsetof(Vertex, color))},
            {2, 0, VK_FORMAT_R32G32_SFLOAT,
             static_cast<uint32_t>(offsetof(Vertex, uv))}};
        config.descriptorSetLayouts.push_back(textureLayout);
        return config;
    }
};

class GraphicsPipeline {
public:
    GraphicsPipeline() = default;
    ~GraphicsPipeline() { reset(); }

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    void initialize(DevicePtr device, VkFormat colorFormat,
                    const PipelineConfig& config) {
        if (!device) {
            throw std::invalid_argument(
                "GraphicsPipeline requires a valid Device");
        }
        device_ = std::move(device);
        colorFormat_ = colorFormat;
        config_ = config;
        create();
    }

    void recreate(VkFormat colorFormat) {
        resetHandles();
        colorFormat_ = colorFormat;
        create();
    }

    VkPipeline handle() const {
        return pipeline_ ? pipeline_->handle() : VK_NULL_HANDLE;
    }
    VkPipelineLayout layout() const {
        return layout_ ? layout_->handle() : VK_NULL_HANDLE;
    }
    void shutdown() { resetHandles(); }

private:
    DevicePtr device_;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    PipelineLayoutPtr layout_;
    PipelinePtr pipeline_;
    PipelineConfig config_;

    ShaderModulePtr createShaderModule(const std::vector<char>& code) const {
        VkShaderModuleCreateInfo createInfo{
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        return std::make_shared<ShaderModule>(device_, createInfo);
    }

    void create() {
        const auto vertexCode = readFile(config_.vertexShader);
        const auto fragmentCode = readFile(config_.fragmentShader);
        const ShaderModulePtr vertexShader = createShaderModule(vertexCode);
        const ShaderModulePtr fragmentShader = createShaderModule(fragmentCode);

        VkPipelineShaderStageCreateInfo vertexStage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertexStage.module = vertexShader->handle();
        vertexStage.pName = "main";
        VkPipelineShaderStageCreateInfo fragmentStage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragmentStage.module = fragmentShader->handle();
        fragmentStage.pName = "main";
        const VkPipelineShaderStageCreateInfo stages[] = {vertexStage,
                                                          fragmentStage};

        VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount =
            static_cast<uint32_t>(config_.vertexBindings.size());
        vertexInput.pVertexBindingDescriptions = config_.vertexBindings.data();
        vertexInput.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(config_.vertexAttributes.size());
        vertexInput.pVertexAttributeDescriptions =
            config_.vertexAttributes.data();
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = config_.topology;

        VkPipelineViewportStateCreateInfo viewportState{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkPipelineRasterizationStateCreateInfo rasterizer{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.polygonMode = config_.polygonMode;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = config_.cullMode;
        rasterizer.frontFace = config_.frontFace;

        VkPipelineMultisampleStateCreateInfo multisampling{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.rasterizationSamples = config_.samples;
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlending{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.size = sizeof(float) * 4;
        const std::vector<VkPushConstantRange> pushConstantRanges{
            pushConstantRange};
        layout_ = std::make_shared<PipelineLayout>(
            device_, config_.descriptorSetLayouts, pushConstantRanges);

        VkPipelineRenderingCreateInfo renderingInfo{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat_;

        VkGraphicsPipelineCreateInfo pipelineInfo{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = layout_->handle();
        pipelineInfo.renderPass = VK_NULL_HANDLE;
        pipeline_ = std::make_shared<Pipeline>(device_, pipelineInfo);
    }

    void resetHandles() {
        pipeline_.reset();
        layout_.reset();
    }

    void reset() {
        if (device_) {
            resetHandles();
        }
    }
};

}  // namespace klib::vk
