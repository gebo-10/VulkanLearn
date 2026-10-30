#include <krender/IRender.h>
#include <krender/backend/vulkan/vulkan_render_impl.h>
#include <krender/material.h>
#include <krender/mesh.h>
#include <krender/resource_manager.h>
#include <krender/shader.h>
#include <krender/shader_utils.h>
#include <krender/slang_compiler.h>

#include <glfw_window.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <glm/gtc/matrix_transform.hpp>

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
    float position[3];
    float normal[3];
    float color[3];
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

        constexpr Vertex vertices[] = {
            {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.90f, 0.28f, 0.18f}},
            {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.90f, 0.28f, 0.18f}},
            {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.90f, 0.28f, 0.18f}},
            {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.90f, 0.28f, 0.18f}},

            {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.20f, 0.62f, 0.92f}},
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.20f, 0.62f, 0.92f}},
            {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.20f, 0.62f, 0.92f}},
            {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.20f, 0.62f, 0.92f}},

            {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.22f, 0.78f, 0.45f}},
            {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.22f, 0.78f, 0.45f}},
            {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.22f, 0.78f, 0.45f}},
            {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.22f, 0.78f, 0.45f}},

            {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.95f, 0.72f, 0.20f}},
            {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {0.95f, 0.72f, 0.20f}},
            {{-0.5f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {0.95f, 0.72f, 0.20f}},
            {{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {0.95f, 0.72f, 0.20f}},

            {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.64f, 0.34f, 0.88f}},
            {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.64f, 0.34f, 0.88f}},
            {{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.64f, 0.34f, 0.88f}},
            {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.64f, 0.34f, 0.88f}},

            {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.20f, 0.78f, 0.82f}},
            {{0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.20f, 0.78f, 0.82f}},
            {{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.20f, 0.78f, 0.82f}},
            {{-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.20f, 0.78f, 0.82f}},
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

            const VkExtent2D renderExtent = context.extent();
            glm::mat4 projection = glm::perspectiveRH_ZO(
                glm::radians(45.0f),
                static_cast<float>(renderExtent.width) /
                    static_cast<float>(renderExtent.height),
                0.1f, 10.0f);
            projection[1][1] *= -1.0f;
            const glm::mat4 view =
                glm::lookAtRH(glm::vec3(2.4f, 1.8f, 2.8f),
                              glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const float time = static_cast<float>(glfwGetTime());
            const glm::mat4 model =
                glm::rotate(glm::mat4(1.0f), time * 0.8f,
                            glm::vec3(0.0f, 1.0f, 0.0f)) *
                glm::rotate(glm::mat4(1.0f), time * 0.5f,
                            glm::vec3(1.0f, 0.0f, 0.0f));

            render->beginFrame();
            if (vulkanRenderer->swapchainRecreationRequired()) {
                continue;
            }
            render->clear(ClearFlag::Color | ClearFlag::Depth,
                          ClearValue{glm::vec4(0.055f, 0.07f, 0.11f, 1.0f),
                                     1.0f, 0});
            render->setViewProjection(view, projection);
            render->renderMesh(cube->handle(), material->handle(), model, 1);
            render->endFrame();
        }

        window.setResizeCallback({});
        return 0;
    } catch (const std::exception& error) {
        spdlog::error("Lit cube demo failed: {}", error.what());
        return 1;
    }
}
