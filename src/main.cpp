#include "renderer.h"
#include "glfw_window.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <spdlog/spdlog.h>

#include <array>
#include <exception>

constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;

int main() {
    spdlog::info("Starting Vulkan 1.4 triangle demo");
    int exitCode = 0;
    try {
        LearnVulkan::GlfwWindow window(
            kWindowWidth, kWindowHeight,
            "Vulkan 1.4 - Triangle");
        LearnVulkan::Renderer renderer(window);

        bool showDemoWindow = true;
        std::array<float, 3> triangleColor{1.0f, 1.0f, 1.0f};
        while (!window.shouldClose()) {
            window.pollEvents();
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(
                0, 
                ImGui::GetMainViewport(), 
                ImGuiDockNodeFlags_PassthruCentralNode  // 核心标志：让中央节点透传
            );
            ImGui::Begin("Triangle");
            if (ImGui::ColorEdit3("Vertex color tint", triangleColor.data())) {
                renderer.setTriangleColor(triangleColor[0], triangleColor[1],
                                          triangleColor[2]);
            }
            ImGui::End();
            ImGui::ShowDemoWindow(&showDemoWindow);
            ImGui::Render();
            renderer.drawFrame(ImGui::GetDrawData());
        }
    } catch (const std::exception& error) {
        spdlog::error("Vulkan demo failed: {}", error.what());
        exitCode = 1;
    }

    return exitCode;
}
