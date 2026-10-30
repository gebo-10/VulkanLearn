#include <krender/resource_manager.h>
#include <krender/shader.h>
#include <krender/slang_compiler.h>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <krender/shader_utils.h>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

using namespace klib::krender;

}  // namespace

int main() {
    using namespace klib::krender;

    //spdlog::set_default_logger(spdlog::stdout_color_mt("shader"));
    spdlog::set_pattern("%^[%l]%$ %v");
    try {
        const std::filesystem::path shaderPath =
            "assets/shaders/triangle.slang";
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

        ResourceManager resourceManager;
        const auto shader = resourceManager.createAs<Shader>(
            ResourceKind::Shader, ZeroRefPolicy::Immediate, "triangle",
            compileResult);

        std::string interfaceError;
        if (!shader->validate(&interfaceError)) {
            throw std::runtime_error("Invalid shader resource: " +
                                     interfaceError);
        }

        shader_utils::logShaderInfo(*shader);
        return 0;
    } catch (const std::exception& error) {
        spdlog::error("Shader resource setup failed: {}", error.what());
        return 1;
    }
}
