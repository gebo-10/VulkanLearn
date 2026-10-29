#pragma once

// =============================================================================
// SlangCompiler.h (Multi-EntryPoint 适配版)
//
// 适配单个 Slang 文件中包含多个 stage（如 vertMain + fragMain）的场景。
// 结果按入口点组织，每个入口点包含独立的 SPIR-V 与反射信息。
// =============================================================================

#include <slang/slang.h>
#include <slang/slang-com-ptr.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace klib::krender {

struct SlangReflectedBinding {
    std::string name;
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t count = 1;
    slang::BindingType type = slang::BindingType::Unknown;
};

struct SlangReflectedPushConstant {
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
};

// 每个入口点的独立编译结果
struct SlangEntryPointResult {
    std::string name;                         // 入口点名称（如 "vertMain"）
    uint32_t stageMask = 0;                   // 该入口点的 stage 位掩码
    std::vector<uint8_t> spirv;               // 该入口点的 SPIR-V 字节码
    std::vector<SlangReflectedBinding> bindings;
    std::vector<SlangReflectedPushConstant> pushConstants;
};

// 整体编译结果（可包含多个入口点）
struct SlangCompileResult {
    std::vector<SlangEntryPointResult> entryPoints;
    std::string diagnostics;
    bool success = false;
};

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
        auto module = session->loadModuleFromSourceString(
            "shader_module", "shader.slang", source.c_str(), diagnostics.writeRef());
        if (!module) {
            assignDiagnostics(diagnostics, result.diagnostics);
            if (result.diagnostics.empty()) result.diagnostics = "Failed to load Slang module";
            return result;
        }

        // 4. 确定要编译的入口点列表
        std::vector<std::string> names = entryPointNames;
        if (names.empty()) {
            // 自动发现模块中所有带 [shader(...)] 的入口点
            const SlangUInt count = module->getEntryPointCount();
            for (SlangUInt i = 0; i < count; ++i) {
                if (auto ep = module->getEntryPointByIndex(i)) {
                    names.emplace_back(ep->getName());
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
            if (SLANG_FAILED(module->findEntryPointByName(epName.c_str(), entryPoint.writeRef()))) {
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
            epResult.name = epName;

            // 5a. 反射（布局按入口点独立计算）
            if (auto layout = program->getLayout(0)) {
                // ProgramLayout 中可能包含多个入口点，找到与当前 name 匹配的那个
                if (auto epLayout = findEntryPointLayout(layout, epName)) {
                    epResult.stageMask = mapStage(epLayout->getStage());
                }
                reflectProgramLayout(layout, epResult);
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
                epResult.spirv.assign(data, data + spirvBlob->getBufferSize());
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
                                     SlangEntryPointResult& out)
    {
        const unsigned paramCount = layout->getParameterCount();
        for (unsigned i = 0; i < paramCount; ++i) {
            auto var = layout->getParameterByIndex(i);
            if (var) reflectVariable(var, out);
        }
    }

    static void reflectVariable(slang::VariableLayoutReflection* var,
                                SlangEntryPointResult& out)
    {
        const auto category = var->getCategory();

        if (category == slang::ParameterCategory::PushConstantBuffer) {
            collectPushConstants(var, 0, "", out);
            return;
        }

        if (category == slang::ParameterCategory::ConstantBuffer ||
            category == slang::ParameterCategory::ShaderResource ||
            category == slang::ParameterCategory::UnorderedAccess ||
            category == slang::ParameterCategory::SamplerState)
        {
            reflectBinding(var, out);
        }
    }

    static void reflectBinding(slang::VariableLayoutReflection* var,
                               SlangEntryPointResult& out)
    {
        SlangReflectedBinding binding;
        const char* name = var->getName();
        binding.name = name ? name : "";
        binding.set = static_cast<uint32_t>(var->getBindingSpace());
        binding.binding = static_cast<uint32_t>(var->getBindingIndex());

        if (auto typeLayout = var->getTypeLayout()) {
            if (typeLayout->getBindingRangeCount() > 0) {
                binding.type = typeLayout->getBindingRangeType(0);
            }
            const auto elemCount = typeLayout->getElementCount();
            if (elemCount > 1) binding.count = static_cast<uint32_t>(elemCount);
        }
        out.bindings.push_back(std::move(binding));
    }

    static void collectPushConstants(slang::VariableLayoutReflection* var,
                                     size_t baseOffset,
                                     const std::string& prefix,
                                     SlangEntryPointResult& out)
    {
        auto typeLayout = var->getTypeLayout();
        if (!typeLayout) return;

        const size_t selfOffset = baseOffset +
            static_cast<size_t>(var->getOffset(slang::ParameterCategory::Uniform));

        if (typeLayout->getKind() == slang::TypeReflection::Kind::Struct) {
            const unsigned fieldCount = typeLayout->getFieldCount();
            for (unsigned i = 0; i < fieldCount; ++i) {
                auto field = typeLayout->getFieldByIndex(i);
                if (!field) continue;
                const char* fieldName = field->getName();
                collectPushConstants(field, selfOffset,
                                     prefix + (fieldName ? fieldName : "") + ".", out);
            }
        } else {
            SlangReflectedPushConstant pc;
            pc.name = prefix.empty()
                          ? std::string(var->getName() ? var->getName() : "")
                          : prefix.substr(0, prefix.size() - 1);
            pc.offset = static_cast<uint32_t>(selfOffset);
            pc.size = static_cast<uint32_t>(typeLayout->getSize());
            out.pushConstants.push_back(std::move(pc));
        }
    }

    static uint32_t mapStage(SlangStage stage) {
        switch (stage) {
            case SLANG_STAGE_VERTEX:   return 1u << 0;
            case SLANG_STAGE_FRAGMENT: return 1u << 1;
            case SLANG_STAGE_COMPUTE:  return 1u << 2;
            case SLANG_STAGE_GEOMETRY: return 1u << 3;
            case SLANG_STAGE_HULL:     return 1u << 4;
            case SLANG_STAGE_DOMAIN:   return 1u << 5;
            default:                   return 0;
        }
    }
};

} // namespace klib::krender