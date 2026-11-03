#include <krender/IRender.h>
#include <krender/backend/vulkan/vulkan_render_impl.h>
#include <krender/material.h>
#include <krender/mesh.h>
#include <krender/render_buffer.h>
#include <krender/render_target.h>
#include <krender/render_texture.h>
#include <krender/resource_manager.h>
#include <krender/shader.h>
#include <krender/shader_utils.h>
#include <krender/slang_compiler.h>

#include <glfw_window.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <stb_image.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Vertex {
    float position[3];
    float normal[3];
    float color[3];
    float uv[2];
};

class DemoImGui {
public:
    ~DemoImGui() { shutdown(); }

    void initialize(GLFWwindow* window, VkInstance instance,
                    klib::krender::VulkanContext& context,
                    VkFormat depthFormat) {
        try {


            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            contextCreated_ = true;
            ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard |
                          ImGuiConfigFlags_DockingEnable;
            ImGui::StyleColorsDark();

        float dpi_scale = 2.0f;
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(dpi_scale); // 缩放内边距、间距、边框等
        style.FontScaleDpi = dpi_scale; // 缩放字体


            if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
                throw std::runtime_error(
                    "Failed to initialize ImGui GLFW backend");
            }
            glfwInitialized_ = true;

            const VkFormat colorFormat =
                context.swapchainOwner()->imageFormat();
            VkPipelineRenderingCreateInfo renderingInfo{
                VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachmentFormats = &colorFormat;
            renderingInfo.depthAttachmentFormat = depthFormat;
            renderingInfo.stencilAttachmentFormat =
                depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                        depthFormat == VK_FORMAT_D24_UNORM_S8_UINT
                    ? depthFormat
                    : VK_FORMAT_UNDEFINED;

            const auto device = context.deviceOwner();
            ImGui_ImplVulkan_InitInfo initInfo{};
            initInfo.ApiVersion = VK_API_VERSION_1_3;
            initInfo.Instance = instance;
            initInfo.PhysicalDevice = device->physicalDevice();
            initInfo.Device = device->handle();
            initInfo.QueueFamily = device->graphicsQueueFamily();
            initInfo.Queue = device->graphicsQueue();
            initInfo.DescriptorPoolSize = 100;
            initInfo.MinImageCount = 2;
            initInfo.ImageCount = context.imageCount();
            initInfo.UseDynamicRendering = true;
            initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            initInfo.PipelineInfoMain.PipelineRenderingCreateInfo =
                renderingInfo;
            if (!ImGui_ImplVulkan_Init(&initInfo)) {
                throw std::runtime_error(
                    "Failed to initialize ImGui Vulkan backend");
            }
            vulkanInitialized_ = true;
            device_ = device->handle();
        } catch (...) {
            shutdown();
            throw;
        }
    }

    void beginFrame() const {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void setImage(klib::krender::RenderTexture& colorTexture,
                  klib::krender::RenderTexture& depthTexture,
                  klib::krender::RenderTarget& target,
                  klib::krender::vulkanimpl::VulkanRenderImpl& renderer,
                  VkDevice device, uint32_t width, uint32_t height) {
        if (width == 0 || height == 0) {
            return;
        }
        if (colorTexture.width() != width ||
            colorTexture.height() != height) {
            if (textureDescriptor_ != VK_NULL_HANDLE) {
                klib::vk::checkVk(vkDeviceWaitIdle(device),
                                  "vkDeviceWaitIdle(ImGui texture resize)");
                ImGui_ImplVulkan_RemoveTexture(textureDescriptor_);
                textureDescriptor_ = VK_NULL_HANDLE;
            }
            colorTexture.resize(width, height);
            depthTexture.resize(width, height);
        }

        renderer.setRenderTarget(target.handle());
        if (textureDescriptor_ == VK_NULL_HANDLE) {
            if (colorTexture.nativeHandle() == VK_NULL_HANDLE) {
                throw std::logic_error(
                    "RenderTexture has no Vulkan image view");
            }
            textureDescriptor_ = ImGui_ImplVulkan_AddTexture(
                colorTexture.nativeHandle(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (textureDescriptor_ == VK_NULL_HANDLE) {
                throw std::runtime_error(
                    "ImGui failed to allocate a RenderTexture descriptor");
            }
        }
    }

    ImTextureRef imageReference() const {
        ImTextureID id{};
        static_assert(sizeof(id) == sizeof(textureDescriptor_),
                      "ImGui texture ID must hold a Vulkan descriptor set");
        std::memcpy(&id, &textureDescriptor_, sizeof(id));
        return ImTextureRef(id);
    }

    void render(VkCommandBuffer commandBuffer) const {
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSet textureDescriptor_ = VK_NULL_HANDLE;
    bool contextCreated_ = false;
    bool glfwInitialized_ = false;
    bool vulkanInitialized_ = false;

    void shutdown() noexcept {
        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
        }
        if (textureDescriptor_ != VK_NULL_HANDLE && vulkanInitialized_) {
            ImGui_ImplVulkan_RemoveTexture(textureDescriptor_);
            textureDescriptor_ = VK_NULL_HANDLE;
        }
        if (vulkanInitialized_) {
            ImGui_ImplVulkan_Shutdown();
            vulkanInitialized_ = false;
        }
        if (glfwInitialized_) {
            ImGui_ImplGlfw_Shutdown();
            glfwInitialized_ = false;
        }
        if (contextCreated_) {
            ImGui::DestroyContext();
            contextCreated_ = false;
        }
    }
};

}  // namespace

int main() {
    using namespace klib::krender;
    using namespace klib::krender::vulkanimpl;
    using namespace klib::vk;

    spdlog::set_default_logger(spdlog::stdout_color_mt("VulkanLearn"));
    spdlog::set_pattern("%^[%l]%$ %v");

    try {
        LearnVulkan::GlfwWindow window(1280, 720, "VulkanLearn - Lit Cube");
        bool framebufferResized = false;
        auto framebufferExtent = window.framebufferExtent();
        while ((framebufferExtent.width == 0 || framebufferExtent.height == 0) &&
               !window.shouldClose()) {
            window.waitEvents();
            framebufferExtent = window.framebufferExtent();
        }
        if (framebufferExtent.width == 0 || framebufferExtent.height == 0) {
            return 0;
        }

        auto instance = std::make_shared<Instance>(
            window.requiredVulkanInstanceExtensions());
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        checkVk(window.createVulkanSurface(instance->handle(), &surface),
                "glfwCreateWindowSurface");
        VulkanContext context(
            instance, surface,
            VkExtent2D{framebufferExtent.width, framebufferExtent.height});

        ResourceManager resourceManager;

        const std::filesystem::path shaderPath =
            std::filesystem::path("assets") / "shaders" / "lit_cube.slang";
        std::ifstream shaderFile(shaderPath, std::ios::binary);
        if (!shaderFile) {
            throw std::runtime_error("Failed to open Slang source: " +
                                     shaderPath.string());
        }
        const std::string source(
            (std::istreambuf_iterator<char>(shaderFile)),
            std::istreambuf_iterator<char>());
        const SlangCompileResult compileResult = SlangCompiler::compile(
            source, {"vertexMain", "fragmentMain"}, "spirv_1_5");
        if (!compileResult.success) {
            throw std::runtime_error("Slang compilation failed: " +
                                     compileResult.diagnostics);
        }

        auto shader = resourceManager.createAs<Shader>(
            ResourceKind::Shader, ZeroRefPolicy::Immediate, "lit_cube",
            compileResult);
        std::string shaderError;
        if (!shader->validate(&shaderError)) {
            throw std::runtime_error("Invalid cube shader: " +
                                     shaderError);
        }
        shader_utils::logShaderInfo(*shader);

        auto material =
            resourceManager.createAs<Material>(ResourceKind::Material);
        material->setShader(shader);
        const std::filesystem::path texturePath =
            std::filesystem::path("assets") / "images" / "coffic.png";
        int textureWidth = 0;
        int textureHeight = 0;
        int textureChannels = 0;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> imageData(
            stbi_load(texturePath.string().c_str(), &textureWidth,
                      &textureHeight, &textureChannels, STBI_rgb_alpha),
            &stbi_image_free);
        if (!imageData) {
            throw std::runtime_error(
                "Failed to load texture " + texturePath.string() + ": " +
                stbi_failure_reason());
        }
        if (textureWidth <= 0 || textureHeight <= 0 ||
            static_cast<uint64_t>(textureWidth) *
                    static_cast<uint64_t>(textureHeight) >
                std::numeric_limits<size_t>::max() / 4) {
            throw std::runtime_error(
                "Texture has invalid dimensions: " + texturePath.string());
        }
        const size_t textureBytes =
            static_cast<size_t>(textureWidth) *
            static_cast<size_t>(textureHeight) * 4;
        std::vector<uint8_t> texturePixels(
            imageData.get(), imageData.get() + textureBytes);
        auto cubeTexture = resourceManager.createAs<Texture>(
            ResourceKind::Texture, ZeroRefPolicy::Immediate,
            static_cast<uint32_t>(textureWidth),
            static_cast<uint32_t>(textureHeight), std::move(texturePixels));
        material->setTexture("cubeTexture", cubeTexture);

        constexpr std::array<std::array<float, 4>, 4> instanceColors{{
            {{1.0f, 0.3f, 0.25f, 1.0f}},
            {{0.25f, 0.8f, 0.4f, 1.0f}},
            {{0.3f, 0.55f, 1.0f, 1.0f}},
            {{1.0f, 0.75f, 0.25f, 1.0f}},
        }};
        auto colorBuffer = resourceManager.createAs<RenderBuffer>(
            ResourceKind::RenderBuffer, ZeroRefPolicy::Immediate,
            instanceColors.data(), sizeof(instanceColors),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        material->setRenderBuffer("instanceColors", colorBuffer);

        constexpr Vertex vertices[] = {
            {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.90f, 0.28f, 0.18f}, {0.0f, 0.0f}},
            {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.90f, 0.28f, 0.18f}, {1.0f, 0.0f}},
            {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.90f, 0.28f, 0.18f}, {1.0f, 1.0f}},
            {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.90f, 0.28f, 0.18f}, {0.0f, 1.0f}},

            {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.20f, 0.62f, 0.92f}, {0.0f, 0.0f}},
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.20f, 0.62f, 0.92f}, {1.0f, 0.0f}},
            {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.20f, 0.62f, 0.92f}, {1.0f, 1.0f}},
            {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.20f, 0.62f, 0.92f}, {0.0f, 1.0f}},

            {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.22f, 0.78f, 0.45f}, {0.0f, 0.0f}},
            {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.22f, 0.78f, 0.45f}, {1.0f, 0.0f}},
            {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.22f, 0.78f, 0.45f}, {1.0f, 1.0f}},
            {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.22f, 0.78f, 0.45f}, {0.0f, 1.0f}},

            {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.95f, 0.72f, 0.20f}, {0.0f, 0.0f}},
            {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {0.95f, 0.72f, 0.20f}, {1.0f, 0.0f}},
            {{-0.5f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {0.95f, 0.72f, 0.20f}, {1.0f, 1.0f}},
            {{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.95f, 0.72f, 0.20f}, {0.0f, 1.0f}},

            {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.64f, 0.34f, 0.88f}, {0.0f, 0.0f}},
            {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.64f, 0.34f, 0.88f}, {1.0f, 0.0f}},
            {{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.64f, 0.34f, 0.88f}, {1.0f, 1.0f}},
            {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.64f, 0.34f, 0.88f}, {0.0f, 1.0f}},

            {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.20f, 0.78f, 0.82f}, {0.0f, 0.0f}},
            {{0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.20f, 0.78f, 0.82f}, {1.0f, 0.0f}},
            {{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.20f, 0.78f, 0.82f}, {1.0f, 1.0f}},
            {{-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.20f, 0.78f, 0.82f}, {0.0f, 1.0f}},
        };
        constexpr uint16_t indices[] = {
            0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7,
            8, 9, 10, 8, 10, 11, 12, 13, 14, 12, 14, 15,
            16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
        };

        MeshData cubeData;
        cubeData.vertices = vertices;
        cubeData.vertexBytes = sizeof(vertices);
        cubeData.vertexCount = static_cast<uint32_t>(std::size(vertices));
        cubeData.layout.stride = sizeof(Vertex);
        cubeData.layout.attributes = {
            {0, VK_FORMAT_R32G32B32_SFLOAT,
             static_cast<uint32_t>(offsetof(Vertex, position))},
            {1, VK_FORMAT_R32G32B32_SFLOAT,
             static_cast<uint32_t>(offsetof(Vertex, normal))},
            {2, VK_FORMAT_R32G32B32_SFLOAT,
             static_cast<uint32_t>(offsetof(Vertex, color))},
            {3, VK_FORMAT_R32G32_SFLOAT,
             static_cast<uint32_t>(offsetof(Vertex, uv))},
        };
        cubeData.indices = indices;
        cubeData.indexBytes = sizeof(indices);
        cubeData.indexCount = static_cast<uint32_t>(std::size(indices));
        cubeData.indexType = VK_INDEX_TYPE_UINT16;
        auto cube = resourceManager.createAs<Mesh>(
            ResourceKind::Mesh, ZeroRefPolicy::Immediate, cubeData);

        auto vulkanRenderer =
            std::make_shared<VulkanRenderImpl>(context, resourceManager);
        std::shared_ptr<IRender> render = vulkanRenderer;
        auto screenTarget = resourceManager.createAs<RenderTarget>(
            ResourceKind::RenderTarget, ZeroRefPolicy::Persistent,
            RenderTarget::Screen);
        render->setRenderTarget(screenTarget->handle());
        const VkExtent2D initialExtent = context.extent();
        auto cubeColorTexture = resourceManager.createAs<RenderTexture>(
            ResourceKind::RenderTexture, ZeroRefPolicy::Persistent,
            initialExtent.width, initialExtent.height);
        const VkFormat depthFormat = vulkanRenderer->screenDepthFormat();
        const VkImageAspectFlags depthAspect =
            VK_IMAGE_ASPECT_DEPTH_BIT |
            (depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                     depthFormat == VK_FORMAT_D24_UNORM_S8_UINT
                 ? VK_IMAGE_ASPECT_STENCIL_BIT
                 : 0);
        auto cubeDepthTexture = resourceManager.createAs<RenderTexture>(
            ResourceKind::RenderTexture, ZeroRefPolicy::Persistent,
            initialExtent.width, initialExtent.height, depthFormat,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            depthAspect);
        auto cubeRenderTarget = resourceManager.createAs<RenderTarget>(
            ResourceKind::RenderTarget, ZeroRefPolicy::Persistent,
            cubeColorTexture, cubeDepthTexture);
        DemoImGui imgui;
        imgui.initialize(window.nativeHandle(), instance->handle(), context,
                         depthFormat);
        std::array<glm::mat4, 3> frameData{
            glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
        auto commonUniformBuffer = resourceManager.createAs<RenderBuffer>(
            ResourceKind::RenderBuffer, ZeroRefPolicy::Immediate,
            frameData.data(), sizeof(frameData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        render->setCommonUniformBuffer(commonUniformBuffer->handle(), 0, 0);
        window.setResizeCallback([&framebufferResized] {
            framebufferResized = true;
        });

        const auto recreateSwapchain = [&]() {
            LearnVulkan::FramebufferExtent extent = window.framebufferExtent();
            while ((extent.width == 0 || extent.height == 0) &&
                   !window.shouldClose()) {
                window.waitEvents();
                extent = window.framebufferExtent();
            }
            if (window.shouldClose()) {
                return false;
            }
            vulkanRenderer->recreateSwapchain(
                VkExtent2D{extent.width, extent.height});
            framebufferResized = false;
            return true;
        };

        while (!window.shouldClose()) {
            window.pollEvents();
            if (framebufferResized ||
                vulkanRenderer->swapchainRecreationRequired()) {
                if (!recreateSwapchain()) {
                    break;
                }
            }

            const glm::mat4 view =
                glm::lookAtRH(glm::vec3(2.4f, 1.8f, 2.8f),
                              glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const float time = static_cast<float>(glfwGetTime());
            glm::mat4 model =
                glm::rotate(glm::mat4(1.0f), time * 0.8f,
                            glm::vec3(0.0f, 1.0f, 0.0f)) *
                glm::rotate(glm::mat4(1.0f), time * 0.5f,
                            glm::vec3(1.0f, 0.0f, 0.0f));

            imgui.beginFrame();

            ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(
                0, 
                ImGui::GetMainViewport(), 
                ImGuiDockNodeFlags_PassthruCentralNode  // 核心标志：让中央节点透传
            );

            ImGui::Begin("Cube View", nullptr);
            const ImVec2 imageSize = ImGui::GetContentRegionAvail();
            const ImVec2 framebufferScale =
                ImGui::GetIO().DisplayFramebufferScale;
            const uint32_t targetWidth = static_cast<uint32_t>(
                std::max(1.0f, std::ceil(imageSize.x * framebufferScale.x)));
            const uint32_t targetHeight = static_cast<uint32_t>(
                std::max(1.0f, std::ceil(imageSize.y * framebufferScale.y)));
            imgui.setImage(*cubeColorTexture, *cubeDepthTexture,
                           *cubeRenderTarget, *vulkanRenderer,
                           context.deviceOwner()->handle(), targetWidth,
                           targetHeight);
            ImGui::Image(imgui.imageReference(), imageSize);
            ImGui::End();

            glm::mat4 cubeProjection = glm::perspectiveRH_ZO(
                glm::radians(45.0f),
                static_cast<float>(cubeColorTexture->width()) /
                    static_cast<float>(cubeColorTexture->height()),
                0.1f, 10.0f);
            cubeProjection[1][1] *= -1.0f;
            frameData[0] = view;
            frameData[1] = cubeProjection;
            frameData[2] = cubeProjection * view;
            commonUniformBuffer->update(frameData.data(), sizeof(frameData));

            render->setRenderTarget(cubeRenderTarget->handle());
            render->beginFrame();
            if (vulkanRenderer->swapchainRecreationRequired()) {
                continue;
            }
            render->clear(ClearFlag::Color | ClearFlag::Depth,
                          ClearValue{glm::vec4(0.055f, 0.07f, 0.11f, 1.0f),
                                     1.0f, 0});
            material->setProperty("model", glm::value_ptr(model),
                                  sizeof(model));
            render->renderMesh(cube->handle(), material->handle(), 1);


            model =  glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 1.0f, 0.0f)) *model;
            material->setProperty("model", glm::value_ptr(model),
                                  sizeof(model));
            render->renderMesh(cube->handle(), material->handle(), 2);

            render->setRenderTarget(screenTarget->handle());
            imgui.render(vulkanRenderer->currentCommandBuffer());
            render->endFrame();
        }

        window.setResizeCallback({});
        return 0;
    } catch (const std::exception& error) {
        spdlog::error("Lit cube demo failed: {}", error.what());
        return 1;
    }
}
