#pragma once

#include <krender/shader.h>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/bundled/color.h>

#include <cstddef>
#include <string>

namespace klib::krender::shader_utils {

inline const char* toString(ShaderStage stage) noexcept {
    switch (stage) {
        case ShaderStage::Vertex: return "vertex";
        case ShaderStage::Fragment: return "fragment";
        case ShaderStage::Compute: return "compute";
        case ShaderStage::Geometry: return "geometry";
        case ShaderStage::TessControl: return "tessellation-control";
        case ShaderStage::TessEval: return "tessellation-evaluation";
        default: return "none";
    }
}

inline const char* toString(BindingType type) noexcept {
    switch (type) {
        case BindingType::UniformBuffer: return "uniform-buffer";
        case BindingType::StorageBuffer: return "storage-buffer";
        case BindingType::SampledImage: return "sampled-image";
        case BindingType::StorageImage: return "storage-image";
        case BindingType::Sampler: return "sampler";
        case BindingType::CombinedImageSampler: return "combined-image-sampler";
        case BindingType::UniformTexelBuffer: return "uniform-texel-buffer";
        case BindingType::StorageTexelBuffer: return "storage-texel-buffer";
        case BindingType::InputAttachment: return "input-attachment";
        case BindingType::AccelerationStructure: return "acceleration-structure";
        case BindingType::Unknown: return "unknown";
    }
    return "unknown";
}

inline const char* toString(ShaderImageDimension dimension) noexcept {
    switch (dimension) {
        case ShaderImageDimension::Buffer: return "buffer";
        case ShaderImageDimension::D1: return "1D";
        case ShaderImageDimension::D2: return "2D";
        case ShaderImageDimension::D3: return "3D";
        case ShaderImageDimension::Cube: return "cube";
        case ShaderImageDimension::SubpassData: return "subpass";
        case ShaderImageDimension::Unknown: return "n/a";
    }
    return "n/a";
}

inline const char* toString(ShaderResourceAccess access) noexcept {
    switch (access) {
        case ShaderResourceAccess::ReadOnly: return "read-only";
        case ShaderResourceAccess::WriteOnly: return "write-only";
        case ShaderResourceAccess::ReadWrite: return "read-write";
    }
    return "unknown";
}

inline const char* toString(ShaderScalarType type) noexcept {
    switch (type) {
        case ShaderScalarType::Bool: return "bool";
        case ShaderScalarType::Int32: return "int32";
        case ShaderScalarType::UInt32: return "uint32";
        case ShaderScalarType::Float32: return "float32";
        case ShaderScalarType::Float64: return "float64";
        case ShaderScalarType::Unknown: return "unknown";
    }
    return "unknown";
}

inline const char* toString(ResourceKind kind) noexcept {
    switch (kind) {
        case ResourceKind::Unknown: return "unknown";
        case ResourceKind::Mesh: return "mesh";
        case ResourceKind::Material: return "material";
        case ResourceKind::Texture: return "texture";
        case ResourceKind::RenderTexture: return "render-texture";
        case ResourceKind::RenderTarget: return "render-target";
        case ResourceKind::Shader: return "shader";
        case ResourceKind::RenderBuffer: return "render-buffer";
    }
    return "unknown";
}

inline const char* toString(ZeroRefPolicy policy) noexcept {
    switch (policy) {
        case ZeroRefPolicy::Evictable: return "evictable";
        case ZeroRefPolicy::Immediate: return "immediate";
        case ZeroRefPolicy::Persistent: return "persistent";
    }
    return "unknown";
}

inline std::string stageMaskToString(ShaderStage stages,
                                     const char* separator = ", ") {
    const ShaderStage allStages[] = {
        ShaderStage::Vertex, ShaderStage::Fragment, ShaderStage::Compute,
        ShaderStage::Geometry, ShaderStage::TessControl, ShaderStage::TessEval};
    std::string result;
    for (ShaderStage stage : allStages) {
        if (!hasShaderStage(stages, stage)) {
            continue;
        }
        if (!result.empty()) {
            result += separator;
        }
        result += toString(stage);
    }
    return result.empty() ? "none" : result;
}

inline void logField(const std::string& key, const std::string& value,
                     const std::string& indent = {}) {
    spdlog::info("{}{}: {}", indent,
                 fmt::styled(key, fmt::fg(fmt::color::light_sky_blue)),
                 fmt::styled(value, fmt::fg(fmt::color::light_green)));
}

inline void logSection(const std::string& title) {
    spdlog::info("{}", fmt::styled(title, fmt::fg(fmt::color::gold)));
}

inline void logInterface(const ShaderInterface& interface) {
    logField("Descriptor bindings",
             fmt::format("{}", interface.bindings.size()), "  ");
    for (const ShaderBinding& binding : interface.bindings) {
        const std::string count =
            binding.count == 0 ? "runtime" : fmt::format("{}", binding.count);
        logField(
            binding.name.empty() ? "<unnamed>" : binding.name,
            fmt::format(
                "set={}, binding={}, type={}, count={}, stages=[{}], "
                "dimension={}, access={}, multisampled={}",
                binding.set, binding.binding, toString(binding.type), count,
                stageMaskToString(binding.stages),
                toString(binding.imageDimension),
                toString(binding.access),
                binding.multisampled ? "true" : "false"),
            "    ");
    }

    logField("Push-constant ranges",
             fmt::format("{}", interface.pushConstantRanges.size()), "  ");
    for (const PushConstantRange& range : interface.pushConstantRanges) {
        logField("Range",
                 fmt::format("offset={}, size={}, stages=[{}], members={}",
                             range.offset, range.size,
                             stageMaskToString(range.stages),
                             range.members.size()),
                 "    ");
        for (const PushConstantMember& member : range.members) {
            const std::string& name =
                member.qualifiedName.empty() ? member.name
                                             : member.qualifiedName;
            logField(
                name,
                fmt::format(
                    "offset={}, size={}, type={} {}x{}, array-count={}, "
                    "array-stride={}, matrix-stride={}, row-major={}",
                    member.offset, member.size, toString(member.type.scalar),
                    static_cast<unsigned>(member.type.rows),
                    static_cast<unsigned>(member.type.columns),
                    member.arrayCount, member.arrayStride, member.matrixStride,
                    member.rowMajor ? "true" : "false"),
                "      ");
        }
    }
}

inline void logShaderInfo(const Shader& shader) {
    logSection("Shader resource");
    logField("name", shader.name(), "  ");
    logField("handle",
             fmt::format("index={}, generation={}", shader.handle().index,
                         shader.handle().generation),
             "  ");
    logField("resource kind", toString(ResourceKind::Shader), "  ");
    logField("zero-reference policy",
             toString(shader.zeroRefPolicy()), "  ");
    logField("resource bytes",
             fmt::format("{}", shader.sizeBytes()), "  ");
    logField("stages",
             fmt::format("{}", shader.stages().size()), "  ");

    for (const ShaderModule& module : shader.stages()) {
        logSection(fmt::format("Stage: {}", toString(module.stage())));
        logField("entry point", module.entryPoint(), "  ");
        logField("code format", "SPIR-V", "  ");
        logField("SPIR-V bytes",
                 fmt::format("{}", module.spirv().size()), "  ");
        logField("push-constant size",
                 fmt::format("{}", module.interface().pushConstantSize()),
                 "  ");
        logInterface(module.interface());
    }

    logSection("Merged shader interface");
    logInterface(shader.interface());
}

}  // namespace klib::krender::shader_utils
