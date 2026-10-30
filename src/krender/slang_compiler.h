#pragma once

// =============================================================================
// SlangCompiler.h (Multi-EntryPoint 适配版)
//
// 适配单个 Slang 文件中包含多个 stage（如 vertMain + fragMain）的场景。
// 结果按入口点组织，每个入口点包含独立的 SPIR-V 与反射信息。
// =============================================================================

#include <krender/shader.h>

#include <slang/slang.h>
#include <slang/slang-com-ptr.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace klib::krender {

struct SlangEntryPointResult {
    ShaderModuleCreateInfo module;
};

struct SlangCompileResult {
    std::vector<SlangEntryPointResult> entryPoints;
    std::string diagnostics;
    bool success = false;
};

namespace detail {

inline bool samePushConstantMember(const PushConstantMember& lhs,
                                   const PushConstantMember& rhs) {
    return lhs.name == rhs.name &&
           lhs.qualifiedName == rhs.qualifiedName &&
           lhs.offset == rhs.offset && lhs.size == rhs.size &&
           lhs.type.scalar == rhs.type.scalar &&
           lhs.type.rows == rhs.type.rows &&
           lhs.type.columns == rhs.type.columns &&
           lhs.arrayCount == rhs.arrayCount &&
           lhs.arrayStride == rhs.arrayStride &&
           lhs.matrixStride == rhs.matrixStride &&
           lhs.rowMajor == rhs.rowMajor;
}

inline bool samePushConstantLayout(const PushConstantRange& lhs,
                                   const PushConstantRange& rhs) {
    if (lhs.offset != rhs.offset || lhs.size != rhs.size ||
        lhs.members.size() != rhs.members.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.members.size(); ++i) {
        if (!samePushConstantMember(lhs.members[i], rhs.members[i])) {
            return false;
        }
    }
    return true;
}

inline void mergeShaderInterface(ShaderInterface& destination,
                                 const ShaderInterface& source) {
    for (const ShaderBinding& binding : source.bindings) {
        auto existing = destination.bindings.begin();
        for (; existing != destination.bindings.end(); ++existing) {
            if (existing->set == binding.set &&
                existing->binding == binding.binding) {
                break;
            }
        }
        if (existing == destination.bindings.end()) {
            destination.bindings.push_back(binding);
            continue;
        }
        if (existing->type != binding.type ||
            existing->count != binding.count ||
            existing->name != binding.name ||
            existing->imageDimension != binding.imageDimension ||
            existing->access != binding.access ||
            existing->multisampled != binding.multisampled) {
            throw std::invalid_argument(
                "Shader stages disagree about a descriptor binding");
        }
        existing->stages |= binding.stages;
    }

    for (const PushConstantRange& range : source.pushConstantRanges) {
        auto existing = destination.pushConstantRanges.begin();
        for (; existing != destination.pushConstantRanges.end(); ++existing) {
            if (samePushConstantLayout(*existing, range)) {
                break;
            }
        }
        if (existing == destination.pushConstantRanges.end()) {
            destination.pushConstantRanges.push_back(range);
        } else {
            existing->stages |= range.stages;
        }
    }
}

}  // namespace detail

inline Shader::Shader(Handle handle, std::string name,
                      const SlangCompileResult& compileResult)
    : ResourceBase(handle, ZeroRefPolicy::Immediate, ResidencyHint::RAM),
      name_(std::move(name)) {
    if (!compileResult.success) {
        throw std::invalid_argument(
            "Cannot create Shader from a failed SlangCompileResult: " +
            compileResult.diagnostics);
    }
    if (compileResult.entryPoints.empty()) {
        throw std::invalid_argument(
            "Cannot create Shader without compiled entry points");
    }

    stages_.reserve(compileResult.entryPoints.size());
    for (const SlangEntryPointResult& entryPoint : compileResult.entryPoints) {
        ShaderModule module(entryPoint.module);
        std::string moduleError;
        if (!module.validate(&moduleError)) {
            throw std::invalid_argument(
                "Invalid compiled shader module: " + moduleError);
        }
        detail::mergeShaderInterface(interface_, module.interface());
        stages_.push_back(std::move(module));
    }

    std::string interfaceError;
    if (!interface_.validate(&interfaceError)) {
        throw std::invalid_argument(
            "Invalid merged shader interface: " + interfaceError);
    }
}

class SlangCompiler {
public:
    // 编译包含多个入口点的 Slang 源码。
    //   source         —— Slang 源码
    //   entryPointNames —— 要编译的入口点名称列表（为空则自动发现模块中所有 [shader(...)] 入口点）
    //   profile        —— SPIR-V 版本
    static SlangCompileResult compile(
        const std::string& source,
        const std::vector<std::string>& entryPointNames = {},
        const std::string& profile = "spirv_1_5",
        const std::vector<std::string>& includePaths = {})
    {
        using Slang::ComPtr;
        SlangCompileResult result;

        // 1. 全局会话
        ComPtr<slang::IGlobalSession> globalSession;
        if (SLANG_FAILED(slang::createGlobalSession(globalSession.writeRef()))) {
            result.diagnostics = "Failed to create global Slang session";
            return result;
        }

        // 2. 会话配置：SPIR-V + 多入口点关键选项
        slang::TargetDesc targetDesc = {};
        targetDesc.format = SLANG_SPIRV;
        targetDesc.profile = globalSession->findProfile(profile.c_str());

        // 关键：EmitSpirvDirectly 确保能直接提取字节码
        slang::CompilerOptionEntry emitOpt = {};
        emitOpt.name = slang::CompilerOptionName::EmitSpirvDirectly;
        emitOpt.value.kind = slang::CompilerOptionValueKind::Int;
        emitOpt.value.intValue0 = 1;

        // 关键：fvk-use-entrypoint-name 防止多入口点被重命名为 "main"
        slang::CompilerOptionEntry nameOpt = {};
        nameOpt.name = slang::CompilerOptionName::VulkanUseEntryPointName;
        nameOpt.value.kind = slang::CompilerOptionValueKind::Int;
        nameOpt.value.intValue0 = 1;

        slang::CompilerOptionEntry options[] = { emitOpt, nameOpt };

        std::vector<const char*> pathViews;
        for (const auto& p : includePaths) pathViews.push_back(p.c_str());

        slang::SessionDesc sessionDesc = {};
        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;
        sessionDesc.compilerOptionEntries = options;
        sessionDesc.compilerOptionEntryCount = 2;
        if (!pathViews.empty()) {
            sessionDesc.searchPaths = pathViews.data();
            sessionDesc.searchPathCount = static_cast<SlangInt>(pathViews.size());
        }

        ComPtr<slang::ISession> session;
        if (SLANG_FAILED(globalSession->createSession(sessionDesc, session.writeRef()))) {
            result.diagnostics = "Failed to create Slang session";
            return result;
        }

        // 3. 加载模块
        ComPtr<slang::IBlob> diagnostics;
        ComPtr<slang::IModule> module;
        module.attach(session->loadModuleFromSourceString(
            "shader_module", "shader.slang", source.c_str(),
            diagnostics.writeRef()));
        if (!module) {
            assignDiagnostics(diagnostics, result.diagnostics);
            if (result.diagnostics.empty()) result.diagnostics = "Failed to load Slang module";
            return result;
        }

        // 4. 确定要编译的入口点列表
        std::vector<std::string> names = entryPointNames;
        if (names.empty()) {
            // 自动发现模块中所有带 [shader(...)] 的入口点
            const SlangInt count = module->getDefinedEntryPointCount();
            for (SlangInt i = 0; i < count; ++i) {
                ComPtr<slang::IEntryPoint> entryPoint;
                if (SLANG_SUCCEEDED(
                        module->getDefinedEntryPoint(i,
                                                     entryPoint.writeRef())) &&
                    entryPoint) {
                    slang::FunctionReflection* function =
                        entryPoint->getFunctionReflection();
                    if (function && function->getName()) {
                        names.emplace_back(function->getName());
                    }
                }
            }
        }
        if (names.empty()) {
            result.diagnostics = "No entry points found or specified";
            return result;
        }

        // 5. 逐个入口点编译
        for (const auto& epName : names) {
            ComPtr<slang::IEntryPoint> entryPoint;
            if (SLANG_FAILED(module->findEntryPointByName(
                    epName.c_str(), entryPoint.writeRef()))) {
                result.diagnostics = "Entry point '" + epName + "' not found";
                return result; // 或选择跳过并记录警告
            }

            // 组合 module + 当前 entryPoint
            slang::IComponentType* components[] = { module.get(), entryPoint.get() };
            ComPtr<slang::IComponentType> program;
            if (SLANG_FAILED(session->createCompositeComponentType(
                    components, 2, program.writeRef(), diagnostics.writeRef()))) {
                assignDiagnostics(diagnostics, result.diagnostics);
                return result;
            }

            SlangEntryPointResult epResult;
            epResult.module.entryPoint = epName;

            // 5a. 反射（布局按入口点独立计算）
            if (auto layout = program->getLayout(0)) {
                // ProgramLayout 中可能包含多个入口点，找到与当前 name 匹配的那个
                if (auto epLayout = findEntryPointLayout(layout, epName)) {
                    epResult.module.stage = mapStage(epLayout->getStage());
                    reflectProgramLayout(
                        layout, epResult.module.interface,
                        epResult.module.stage);
                }
            }
            if (epResult.module.stage == ShaderStage::None) {
                result.diagnostics =
                    "Failed to reflect stage for entry point '" + epName + "'";
                return result;
            }

            // 5b. 提取该入口点的 SPIR-V
            // 注意：getEntryPointCode 的 entryPointIndex 是当前 program 内的索引
            // 由于 program 只组合了一个 entryPoint，索引为 0
            ComPtr<slang::IBlob> spirvBlob;
            if (SLANG_FAILED(program->getEntryPointCode(0, 0, spirvBlob.writeRef(), diagnostics.writeRef()))) {
                assignDiagnostics(diagnostics, result.diagnostics);
                return result;
            }
            if (spirvBlob) {
                const auto* data = static_cast<const uint8_t*>(spirvBlob->getBufferPointer());
                epResult.module.spirv.assign(
                    data, data + spirvBlob->getBufferSize());
            }
            if (epResult.module.spirv.empty()) {
                result.diagnostics =
                    "Slang produced no SPIR-V for entry point '" + epName + "'";
                return result;
            }

            std::string interfaceError;
            if (!epResult.module.interface.validate(&interfaceError)) {
                result.diagnostics =
                    "Invalid reflection for entry point '" + epName +
                    "': " + interfaceError;
                return result;
            }

            result.entryPoints.push_back(std::move(epResult));
        }

        result.success = true;
        assignDiagnostics(diagnostics, result.diagnostics);
        return result;
    }

private:
    static void assignDiagnostics(const Slang::ComPtr<slang::IBlob>& diag, std::string& out) {
        if (!diag) return;
        const auto* text = static_cast<const char*>(diag->getBufferPointer());
        if (text && diag->getBufferSize() > 0) {
            out.assign(text, diag->getBufferSize());
        }
    }

    static slang::EntryPointLayout* findEntryPointLayout(
        slang::ProgramLayout* layout, const std::string& name)
    {
        const unsigned count = layout->getEntryPointCount();
        for (unsigned i = 0; i < count; ++i) {
            auto ep = layout->getEntryPointByIndex(i);
            if (ep && name == ep->getName()) return ep;
        }
        return nullptr;
    }

    static void reflectProgramLayout(slang::ProgramLayout* layout,
                                     ShaderInterface& interface,
                                     ShaderStage stage)
    {
        const unsigned paramCount = layout->getParameterCount();
        for (unsigned i = 0; i < paramCount; ++i) {
            auto var = layout->getParameterByIndex(i);
            if (var) reflectVariable(var, interface, stage);
        }
    }

    static void reflectVariable(slang::VariableLayoutReflection* var,
                                ShaderInterface& interface,
                                ShaderStage stage)
    {
        const auto category = var->getCategory();

        if (category == slang::ParameterCategory::PushConstantBuffer) {
            reflectPushConstantRange(var, stage, interface);
            return;
        }
        if (category == slang::ParameterCategory::VaryingInput ||
            category == slang::ParameterCategory::VaryingOutput) {
            return;
        }

        slang::TypeLayoutReflection* typeLayout = var->getTypeLayout();
        if (typeLayout &&
            (typeLayout->getBindingRangeCount() > 0 ||
             category == slang::ParameterCategory::ConstantBuffer ||
             category == slang::ParameterCategory::ShaderResource ||
             category == slang::ParameterCategory::UnorderedAccess ||
             category == slang::ParameterCategory::SamplerState)) {
            reflectBinding(var, stage, interface);
        }
    }

    static void reflectBinding(slang::VariableLayoutReflection* var,
                               ShaderStage stage,
                               ShaderInterface& interface)
    {
        ShaderBinding binding;
        const char* name = var->getName();
        binding.name = name ? name : "";
        binding.set = static_cast<uint32_t>(var->getBindingSpace());
        binding.binding = static_cast<uint32_t>(var->getBindingIndex());
        binding.stages = stage;

        if (auto typeLayout = var->getTypeLayout()) {
            if (typeLayout->getBindingRangeCount() > 0) {
                binding.type = mapBindingType(
                    typeLayout->getBindingRangeType(0));
            }
            const auto elemCount = typeLayout->getElementCount();
            if (elemCount == SLANG_UNBOUNDED_SIZE) {
                binding.count = 0;
            } else if (elemCount > 1 &&
                       elemCount <= std::numeric_limits<uint32_t>::max()) {
                binding.count = static_cast<uint32_t>(elemCount);
            }
            if (slang::TypeReflection* type = typeLayout->getType()) {
                const SlangResourceShape shape = type->getResourceShape();
                switch (shape & SLANG_RESOURCE_BASE_SHAPE_MASK) {
                    case SLANG_TEXTURE_BUFFER:
                        binding.imageDimension = ShaderImageDimension::Buffer;
                        break;
                    case SLANG_TEXTURE_1D:
                        binding.imageDimension = ShaderImageDimension::D1;
                        break;
                    case SLANG_TEXTURE_2D:
                        binding.imageDimension = ShaderImageDimension::D2;
                        break;
                    case SLANG_TEXTURE_3D:
                        binding.imageDimension = ShaderImageDimension::D3;
                        break;
                    case SLANG_TEXTURE_CUBE:
                        binding.imageDimension = ShaderImageDimension::Cube;
                        break;
                    case SLANG_TEXTURE_SUBPASS:
                        binding.imageDimension =
                            ShaderImageDimension::SubpassData;
                        break;
                    default:
                        break;
                }
                binding.multisampled =
                    (shape & SLANG_TEXTURE_MULTISAMPLE_FLAG) != 0;
                switch (type->getResourceAccess()) {
                    case SLANG_RESOURCE_ACCESS_READ:
                        binding.access = ShaderResourceAccess::ReadOnly;
                        break;
                    case SLANG_RESOURCE_ACCESS_WRITE:
                        binding.access = ShaderResourceAccess::WriteOnly;
                        break;
                    case SLANG_RESOURCE_ACCESS_READ_WRITE:
                        binding.access = ShaderResourceAccess::ReadWrite;
                        break;
                    default:
                        break;
                }
            }
        }
        interface.bindings.push_back(std::move(binding));
    }

    static void reflectPushConstantRange(
        slang::VariableLayoutReflection* var, ShaderStage stage,
        ShaderInterface& interface)
    {
        slang::TypeLayoutReflection* containerLayout = var->getTypeLayout();
        if (!containerLayout) {
            return;
        }
        slang::TypeLayoutReflection* valueLayout =
            containerLayout->getElementTypeLayout();
        if (!valueLayout) {
            valueLayout = containerLayout;
        }

        const size_t reflectedSize =
            valueLayout->getSize(slang::ParameterCategory::Uniform);
        if (reflectedSize == 0 ||
            reflectedSize > std::numeric_limits<uint32_t>::max()) {
            return;
        }

        PushConstantRange range;
        range.stages = stage;
        range.size = static_cast<uint32_t>(reflectedSize);
        const char* variableName = var->getName();
        const std::string prefix =
            variableName ? std::string(variableName) + "." : std::string{};
        collectPushConstantMembers(valueLayout, 0, prefix, range.members);
        interface.pushConstantRanges.push_back(std::move(range));
    }

    static void collectPushConstantMembers(
        slang::TypeLayoutReflection* typeLayout, size_t baseOffset,
        const std::string& prefix, std::vector<PushConstantMember>& members)
    {
        if (!typeLayout ||
            typeLayout->getKind() != slang::TypeReflection::Kind::Struct) {
            return;
        }

        const unsigned fieldCount = typeLayout->getFieldCount();
        for (unsigned i = 0; i < fieldCount; ++i) {
            slang::VariableLayoutReflection* field =
                typeLayout->getFieldByIndex(i);
            if (!field) {
                continue;
            }

            const char* rawName = field->getName();
            const std::string fieldName = rawName ? rawName : "";
            slang::TypeLayoutReflection* fieldLayout = field->getTypeLayout();
            if (!fieldLayout) {
                continue;
            }

            const size_t offset =
                baseOffset +
                field->getOffset(slang::ParameterCategory::Uniform);
            const std::string qualifiedName = prefix + fieldName;
            if (fieldLayout->getKind() ==
                slang::TypeReflection::Kind::Struct) {
                collectPushConstantMembers(fieldLayout, offset,
                                           qualifiedName + ".", members);
                continue;
            }

            const size_t reflectedSize =
                fieldLayout->getSize(slang::ParameterCategory::Uniform);
            if (fieldName.empty() || reflectedSize == 0 ||
                offset > std::numeric_limits<uint32_t>::max() ||
                reflectedSize > std::numeric_limits<uint32_t>::max()) {
                continue;
            }

            PushConstantMember member;
            member.name = fieldName;
            member.qualifiedName = qualifiedName;
            member.offset = static_cast<uint32_t>(offset);
            member.size = static_cast<uint32_t>(reflectedSize);
            slang::TypeLayoutReflection* valueLayout = fieldLayout;
            slang::TypeReflection* type = valueLayout->getType();
            if (type && type->isArray()) {
                const size_t count = type->getTotalArrayElementCount();
                if (count <= std::numeric_limits<uint32_t>::max()) {
                    member.arrayCount = static_cast<uint32_t>(count);
                }
                member.arrayStride = static_cast<uint32_t>(
                    std::min(valueLayout->getStride(
                                 slang::ParameterCategory::Uniform),
                             static_cast<size_t>(
                                 std::numeric_limits<uint32_t>::max())));
                valueLayout = valueLayout->unwrapArray();
                type = valueLayout ? valueLayout->getType() : nullptr;
            }
            if (type) {
                member.type.scalar = mapScalarType(type->getScalarType());
                member.type.rows = static_cast<uint8_t>(
                    std::min(type->getRowCount(), 255u));
                member.type.columns = static_cast<uint8_t>(
                    std::min(type->getColumnCount(), 255u));
                if (type->getKind() == slang::TypeReflection::Kind::Matrix) {
                    member.rowMajor =
                        valueLayout->getMatrixLayoutMode() ==
                        SLANG_MATRIX_LAYOUT_ROW_MAJOR;
                    const uint32_t majorCount =
                        member.rowMajor ? member.type.rows
                                        : member.type.columns;
                    const uint32_t minorCount =
                        member.rowMajor ? member.type.columns
                                        : member.type.rows;
                    const uint32_t scalarSize =
                        member.type.scalar == ShaderScalarType::Float64
                            ? sizeof(double)
                            : sizeof(float);
                    const size_t matrixSize =
                        valueLayout->getSize(
                            slang::ParameterCategory::Uniform);
                    const size_t packedStride =
                        static_cast<size_t>(minorCount) * scalarSize;
                    const size_t reflectedStride =
                        majorCount != 0 && matrixSize % majorCount == 0
                            ? matrixSize / majorCount
                            : packedStride;
                    member.matrixStride = static_cast<uint32_t>(
                        std::min(std::max(reflectedStride, packedStride),
                                 static_cast<size_t>(
                                     std::numeric_limits<uint32_t>::max())));
                }
            }
            members.push_back(std::move(member));
        }
    }

    static BindingType mapBindingType(slang::BindingType type)
    {
        switch (type) {
            case slang::BindingType::Sampler:
                return BindingType::Sampler;
            case slang::BindingType::Texture:
                return BindingType::SampledImage;
            case slang::BindingType::ConstantBuffer:
            case slang::BindingType::ParameterBlock:
            case slang::BindingType::InlineUniformData:
                return BindingType::UniformBuffer;
            case slang::BindingType::TypedBuffer:
                return BindingType::UniformTexelBuffer;
            case slang::BindingType::RawBuffer:
            case slang::BindingType::MutableRawBuffer:
                return BindingType::StorageBuffer;
            case slang::BindingType::CombinedTextureSampler:
                return BindingType::CombinedImageSampler;
            case slang::BindingType::InputRenderTarget:
                return BindingType::InputAttachment;
            case slang::BindingType::RayTracingAccelerationStructure:
                return BindingType::AccelerationStructure;
            case slang::BindingType::MutableTexture:
                return BindingType::StorageImage;
            case slang::BindingType::MutableTypedBuffer:
                return BindingType::StorageTexelBuffer;
            default:
                return BindingType::Unknown;
        }
    }

    static ShaderScalarType mapScalarType(
        slang::TypeReflection::ScalarType type)
    {
        switch (type) {
            case slang::TypeReflection::Bool:
                return ShaderScalarType::Bool;
            case slang::TypeReflection::Int32:
                return ShaderScalarType::Int32;
            case slang::TypeReflection::UInt32:
                return ShaderScalarType::UInt32;
            case slang::TypeReflection::Float32:
                return ShaderScalarType::Float32;
            case slang::TypeReflection::Float64:
                return ShaderScalarType::Float64;
            default:
                return ShaderScalarType::Unknown;
        }
    }

    static ShaderStage mapStage(SlangStage stage) {
        switch (stage) {
            case SLANG_STAGE_VERTEX:   return ShaderStage::Vertex;
            case SLANG_STAGE_FRAGMENT: return ShaderStage::Fragment;
            case SLANG_STAGE_COMPUTE:  return ShaderStage::Compute;
            case SLANG_STAGE_GEOMETRY: return ShaderStage::Geometry;
            case SLANG_STAGE_HULL:     return ShaderStage::TessControl;
            case SLANG_STAGE_DOMAIN:   return ShaderStage::TessEval;
            default:                   return ShaderStage::None;
        }
    }
};

} // namespace klib::krender