#include <krender/IRender.h>
#include <krender/backend/vulkan/vulkan_render_impl.h>
#include <krender/material.h>
#include <krender/mesh.h>
#include <krender/resource_manager.h>
#include <krender/shader.h>
#include <krender/shader_utils.h>
#include <krender/slang_compiler.h>
#include <krender/texture.h>

#include <glfw_window.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Vertex {
    float position[2];
    float color[3];
    float uv[2];
};

}  // namespace

int main() {
    using namespace klib::krender;
    using namespace klib::krender::vulkanimpl;
    using namespace klib::vk;

    spdlog::set_default_logger(spdlog::stdout_color_mt("VulkanLearn"));
    spdlog::set_pattern("%^[%l]%$ %v");

    try {
        LearnVulkan::GlfwWindow window(1280, 720, "VulkanLearn");
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
            std::filesystem::path("assets") / "shaders" / "triangle.slang";
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
            ResourceKind::Shader, ZeroRefPolicy::Immediate, "rectangle",
            compileResult);
        std::string shaderError;
        if (!shader->validate(&shaderError)) {
            throw std::runtime_error("Invalid rectangle shader: " +
                                     shaderError);
        }
        shader_utils::logShaderInfo(*shader);

        std::vector<uint8_t> whitePixel{255, 255, 255, 255};
        auto texture = resourceManager.createAs<Texture>(
            ResourceKind::Texture, ZeroRefPolicy::Immediate, 1, 1,
            std::move(whitePixel));

        auto material =
            resourceManager.createAs<Material>(ResourceKind::Material);
        material->setShader(shader);
        material->setTexture("triangleTexture", texture);
        material->setProperty(
            "triangleParameters.color",
            std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f});

        constexpr Vertex vertices[] = {
            {{-0.65f, -0.5f}, {1.0f, 0.25f, 0.15f}, {0.0f, 0.0f}},
            {{0.65f, -0.5f}, {0.2f, 0.85f, 0.35f}, {1.0f, 0.0f}},
            {{0.65f, 0.5f}, {0.2f, 0.45f, 1.0f}, {1.0f, 1.0f}},
            {{-0.65f, 0.5f}, {1.0f, 0.8f, 0.2f}, {0.0f, 1.0f}},
        };
        constexpr uint16_t indices[] = {0, 1, 2, 0, 2, 3};

        MeshData rectangleData;
        rectangleData.vertices = vertices;
        rectangleData.vertexBytes = sizeof(vertices);
        rectangleData.vertexCount = static_cast<uint32_t>(std::size(vertices));
        rectangleData.layout.stride = sizeof(Vertex);
        rectangleData.layout.attributes = {
            {0, VK_FORMAT_R32G32_SFLOAT,
             static_cast<uint32_t>(offsetof(Vertex, position))},
            {1, VK_FORMAT_R32G32B32_SFLOAT,
             static_cast<uint32_t>(offsetof(Vertex, color))},
            {2, VK_FORMAT_R32G32_SFLOAT,
             static_cast<uint32_t>(offsetof(Vertex, uv))},
        };
        rectangleData.indices = indices;
        rectangleData.indexBytes = sizeof(indices);
        rectangleData.indexCount = static_cast<uint32_t>(std::size(indices));
        rectangleData.indexType = VK_INDEX_TYPE_UINT16;
        auto rectangle = resourceManager.createAs<Mesh>(
            ResourceKind::Mesh, ZeroRefPolicy::Immediate, rectangleData);

        auto vulkanRenderer =
            std::make_shared<VulkanRenderImpl>(context, resourceManager);
        std::shared_ptr<IRender> render = vulkanRenderer;
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

            render->beginFrame();
            if (vulkanRenderer->swapchainRecreationRequired()) {
                continue;
            }
            render->clear(ClearFlag::Color | ClearFlag::Depth,
                          ClearValue{glm::vec4(0.055f, 0.07f, 0.11f, 1.0f),
                                     1.0f, 0});
            render->renderMesh(rectangle->handle(), material->handle(),
                               glm::mat4(1.0f), 1);
            render->endFrame();
        }

        window.setResizeCallback({});
        return 0;
    } catch (const std::exception& error) {
        spdlog::error("Vulkan rectangle demo failed: {}", error.what());
        return 1;
    }
}
