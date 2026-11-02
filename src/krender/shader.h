#pragma once

#include <krender/resource_base.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


namespace klib::krender {

struct SlangCompileResult;

enum class ShaderStage : uint32_t {
    None        = 0,
    Vertex      = 1u << 0,
    Fragment    = 1u << 1,
    Compute     = 1u << 2,
    Geometry    = 1u << 3,
    TessControl = 1u << 4,
    TessEval    = 1u << 5,
};

constexpr ShaderStage operator|(ShaderStage lhs, ShaderStage rhs) noexcept {
    return static_cast<ShaderStage>(static_cast<uint32_t>(lhs) |
                                    static_cast<uint32_t>(rhs));
}

constexpr ShaderStage operator&(ShaderStage lhs, ShaderStage rhs) noexcept {
    return static_cast<ShaderStage>(static_cast<uint32_t>(lhs) &
                                    static_cast<uint32_t>(rhs));
}

constexpr ShaderStage& operator|=(ShaderStage& lhs, ShaderStage rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool hasShaderStage(ShaderStage stages, ShaderStage stage) noexcept {
    return (stages & stage) != ShaderStage::None;
}

enum class BindingType : uint8_t {
    UniformBuffer,
    StorageBuffer,
    SampledImage,
    StorageImage,
    Sampler,
    CombinedImageSampler,
    UniformTexelBuffer,
    StorageTexelBuffer,
    InputAttachment,
    AccelerationStructure,
    Unknown,
};

enum class ShaderImageDimension : uint8_t {
    Unknown,
    Buffer,
    D1,
    D2,
    D3,
    Cube,
    SubpassData,
};

enum class ShaderResourceAccess : uint8_t {
    ReadOnly,
    WriteOnly,
    ReadWrite,
};

struct ShaderBinding {
    uint32_t set = 0;      // Logical descriptor set; GL backends map this to binding points.
    uint32_t binding = 0;
    BindingType type = BindingType::UniformBuffer;
    uint32_t count = 1;  // Zero denotes a runtime-sized descriptor array.
    std::string name;
    ShaderStage stages = ShaderStage::None;
    ShaderImageDimension imageDimension = ShaderImageDimension::Unknown;
    ShaderResourceAccess access = ShaderResourceAccess::ReadOnly;
    bool multisampled = false;
};

enum class ShaderScalarType : uint8_t {
    Unknown,
    Bool,
    Int32,
    UInt32,
    Float32,
    Float64,
};

struct ShaderValueType {
    ShaderScalarType scalar = ShaderScalarType::Unknown;
    uint8_t rows = 1;
    uint8_t columns = 1;
};

struct PushConstantMember {
    std::string name;
    std::string qualifiedName;
    uint32_t offset = 0;
    uint32_t size = 0;
    ShaderValueType type;
    uint32_t arrayCount = 1;
    uint32_t arrayStride = 0;
    uint32_t matrixStride = 0;
    bool rowMajor = false;
};

struct PushConstantRange {
    ShaderStage stages = ShaderStage::None;
    uint32_t offset = 0;
    uint32_t size = 0;
    // OpenGL backends can represent this range with a uniform buffer.
    std::vector<PushConstantMember> members;
};

struct PushConstantField {
    std::string propertyName;
    std::string qualifiedName;
    uint32_t offset = 0;
    uint32_t size = 0;
    ShaderValueType type;
    uint32_t arrayCount = 1;
    uint32_t arrayStride = 0;
    uint32_t matrixStride = 0;
    bool rowMajor = false;
};

struct PushConstantLayout {
    uint32_t size = 0;
    std::vector<PushConstantField> fields;

    const PushConstantField* findField(
        const std::string& name) const noexcept {
        const PushConstantField* match = nullptr;
        for (const PushConstantField& field : fields) {
            if (field.qualifiedName == name) {
                return &field;
            }
            if (field.propertyName == name) {
                if (match != nullptr) {
                    return nullptr;
                }
                match = &field;
            }
        }
        return match;
    }
};

struct ShaderInterface {
    std::vector<ShaderBinding> bindings;
    std::vector<PushConstantRange> pushConstantRanges;

    const ShaderBinding* findBinding(const std::string& name) const noexcept {
        for (const ShaderBinding& binding : bindings) {
            if (binding.name == name) {
                return &binding;
            }
        }
        return nullptr;
    }

    const PushConstantMember* findPushConstantMember(
        const std::string& name) const noexcept {
        for (const PushConstantRange& range : pushConstantRanges) {
            for (const PushConstantMember& member : range.members) {
                if (member.name == name || member.qualifiedName == name) {
                    return &member;
                }
            }
        }
        return nullptr;
    }

    uint32_t pushConstantSize() const noexcept {
        uint32_t maxSize = 0;
        for (const PushConstantRange& range : pushConstantRanges) {
            if (range.offset > std::numeric_limits<uint32_t>::max() -
                                   range.size) {
                return std::numeric_limits<uint32_t>::max();
            }
            const uint32_t rangeEnd = range.offset + range.size;
            if (rangeEnd > maxSize) {
                maxSize = rangeEnd;
            }
        }
        return maxSize;
    }

    bool empty() const noexcept {
        return bindings.empty() && pushConstantRanges.empty();
    }

    bool validate(std::string* error = nullptr) const {
        const auto fail = [error](const char* message) {
            if (error) {
                *error = message;
            }
            return false;
        };

        for (size_t i = 0; i < bindings.size(); ++i) {
            const ShaderBinding& binding = bindings[i];
            for (size_t j = 0; j < i; ++j) {
                if (bindings[j].set == binding.set &&
                    bindings[j].binding == binding.binding) {
                    return fail("Duplicate shader descriptor set and binding");
                }
                if (!binding.name.empty() &&
                    bindings[j].name == binding.name) {
                    return fail("Duplicate shader descriptor name");
                }
            }
            if (binding.stages == ShaderStage::None) {
                return fail("Shader descriptor has no stage visibility");
            }
            if (binding.type == BindingType::Unknown) {
                return fail("Shader descriptor has an unsupported binding type");
            }
        }

        for (size_t i = 0; i < pushConstantRanges.size(); ++i) {
            const PushConstantRange& range = pushConstantRanges[i];
            if (range.size == 0) {
                return fail("Push-constant range must not be empty");
            }
            if (range.stages == ShaderStage::None) {
                return fail("Push-constant range has no stage visibility");
            }
            if (range.offset >
                std::numeric_limits<uint32_t>::max() - range.size) {
                return fail("Push-constant range overflows its address space");
            }
            const uint32_t rangeEnd = range.offset + range.size;
            for (size_t j = 0; j < i; ++j) {
                const PushConstantRange& previous = pushConstantRanges[j];
                if ((previous.stages & range.stages) != ShaderStage::None &&
                    previous.offset < rangeEnd &&
                    range.offset < previous.offset + previous.size) {
                    return fail("Overlapping push-constant ranges share stages");
                }
            }
            for (size_t memberIndex = 0;
                 memberIndex < range.members.size(); ++memberIndex) {
                const PushConstantMember& member =
                    range.members[memberIndex];
                if (member.name.empty() || member.size == 0) {
                    return fail("Push-constant members require a name and size");
                }
                if (member.offset < range.offset ||
                    member.offset > rangeEnd ||
                    member.size > rangeEnd - member.offset) {
                    return fail("Push-constant member exceeds its range");
                }
                for (size_t previousIndex = 0;
                     previousIndex < memberIndex; ++previousIndex) {
                    if (range.members[previousIndex].name == member.name) {
                        return fail("Duplicate push-constant member name");
                    }
                }
            }
        }
        return true;
    }

    bool operator==(const ShaderInterface& other) const noexcept {
        if (bindings.size() != other.bindings.size() ||
            pushConstantRanges.size() != other.pushConstantRanges.size()) {
            return false;
        }

        for (size_t i = 0; i < bindings.size(); ++i) {
            const ShaderBinding& lhs = bindings[i];
            const ShaderBinding& rhs = other.bindings[i];
            if (lhs.set != rhs.set || lhs.binding != rhs.binding ||
                lhs.type != rhs.type || lhs.count != rhs.count ||
                lhs.name != rhs.name || lhs.stages != rhs.stages ||
                lhs.imageDimension != rhs.imageDimension ||
                lhs.access != rhs.access ||
                lhs.multisampled != rhs.multisampled) {
                return false;
            }
        }

        for (size_t i = 0; i < pushConstantRanges.size(); ++i) {
            const PushConstantRange& lhs = pushConstantRanges[i];
            const PushConstantRange& rhs = other.pushConstantRanges[i];
            if (lhs.stages != rhs.stages || lhs.offset != rhs.offset ||
                lhs.size != rhs.size || lhs.members.size() != rhs.members.size()) {
                return false;
            }
            for (size_t j = 0; j < lhs.members.size(); ++j) {
                const PushConstantMember& left = lhs.members[j];
                const PushConstantMember& right = rhs.members[j];
                if (left.name != right.name ||
                    left.qualifiedName != right.qualifiedName ||
                    left.offset != right.offset ||
                    left.size != right.size ||
                    left.type.scalar != right.type.scalar ||
                    left.type.rows != right.type.rows ||
                    left.type.columns != right.type.columns ||
                    left.arrayCount != right.arrayCount ||
                    left.arrayStride != right.arrayStride ||
                    left.matrixStride != right.matrixStride ||
                    left.rowMajor != right.rowMajor) {
                    return false;
                }
            }
        }
        return true;
    }

    uint64_t fingerprint() const noexcept {
        uint64_t hash = 14695981039346656037ull;
        const auto mix = [&hash](uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ull;
        };
        const auto mixString = [&mix](const std::string& value) {
            mix(value.size());
            for (unsigned char character : value) {
                mix(character);
            }
        };

        mix(bindings.size());
        for (const ShaderBinding& binding : bindings) {
            mix(binding.set);
            mix(binding.binding);
            mix(static_cast<uint8_t>(binding.type));
            mix(binding.count);
            mixString(binding.name);
            mix(static_cast<uint32_t>(binding.stages));
            mix(static_cast<uint8_t>(binding.imageDimension));
            mix(static_cast<uint8_t>(binding.access));
            mix(binding.multisampled);
        }
        mix(pushConstantRanges.size());
        for (const PushConstantRange& range : pushConstantRanges) {
            mix(static_cast<uint32_t>(range.stages));
            mix(range.offset);
            mix(range.size);
            mix(range.members.size());
            for (const PushConstantMember& member : range.members) {
                mixString(member.name);
                mixString(member.qualifiedName);
                mix(member.offset);
                mix(member.size);
                mix(static_cast<uint8_t>(member.type.scalar));
                mix(member.type.rows);
                mix(member.type.columns);
                mix(member.arrayCount);
                mix(member.arrayStride);
                mix(member.matrixStride);
                mix(member.rowMajor);
            }
        }
        return hash;
    }
};

// Backend-neutral exchange type shared by shader compilers and ShaderModule.
struct ShaderModuleCreateInfo {
    ShaderStage stage = ShaderStage::None;
    std::string entryPoint = "main";
    std::vector<uint8_t> spirv;
    ShaderInterface interface;
};

class ShaderModule {
public:
    ShaderModule(ShaderStage stage, std::vector<uint8_t> spirv,
                 std::string entryPoint = "main",
                 ShaderInterface shaderInterface = {})
        : stage_(stage),
          spirv_(std::move(spirv)),
          entryPoint_(std::move(entryPoint)),
          interface_(std::move(shaderInterface)) {}

    explicit ShaderModule(ShaderModuleCreateInfo createInfo)
        : ShaderModule(createInfo.stage, std::move(createInfo.spirv),
                       std::move(createInfo.entryPoint),
                       std::move(createInfo.interface)) {}

    ShaderStage stage() const noexcept { return stage_; }
    const std::vector<uint8_t>& spirv() const noexcept { return spirv_; }
    const std::string& entryPoint() const noexcept { return entryPoint_; }
    const ShaderInterface& interface() const noexcept { return interface_; }

    bool validate(std::string* error = nullptr) const {
        const auto fail = [error](const char* message) {
            if (error) {
                *error = message;
            }
            return false;
        };
        const uint32_t stageBits = static_cast<uint32_t>(stage_);
        if (stageBits == 0 || (stageBits & (stageBits - 1)) != 0 ||
            (stageBits & ~uint32_t{0x3f}) != 0) {
            return fail("Shader module must have exactly one supported stage");
        }
        if (spirv_.empty()) {
            return fail("Shader module code must not be empty");
        }
        if (entryPoint_.empty()) {
            return fail("Shader entry point must not be empty");
        }
        if (spirv_.size() < sizeof(uint32_t) ||
            spirv_.size() % sizeof(uint32_t) != 0 ||
            spirv_[0] != 0x03 || spirv_[1] != 0x02 ||
            spirv_[2] != 0x23 || spirv_[3] != 0x07) {
            return fail("SPIR-V code has an invalid header or size");
        }
        return interface_.validate(error);
    }

    uint64_t sizeBytes() const noexcept {
        return sizeof(*this) + spirv_.size() + entryPoint_.size();
    }

private:
    ShaderStage stage_;
    std::vector<uint8_t> spirv_;
    std::string entryPoint_;
    ShaderInterface interface_;
};

class Shader : public ResourceBase {
public:
    Shader(Handle handle, std::string name,
           std::vector<ShaderModule> stages,
           ShaderInterface mergedInterface = {})
        : ResourceBase(handle, ZeroRefPolicy::Immediate, ResidencyHint::RAM),
          name_(std::move(name)),
          stages_(std::move(stages)),
          interface_(std::move(mergedInterface)) {}

    Shader(Handle handle, std::string name,
           const SlangCompileResult& compileResult);

    const std::string& name() const noexcept { return name_; }
    const std::vector<ShaderModule>& stages() const noexcept {
        return stages_;
    }
    const ShaderInterface& interface() const noexcept { return interface_; }

    PushConstantLayout pushConstantLayout() const {
        std::string error;
        if (!interface_.validate(&error)) {
            throw std::invalid_argument(
                "Cannot build push-constant layout from invalid shader: " +
                error);
        }
        PushConstantLayout layout;
        layout.size = interface_.pushConstantSize();
        for (const PushConstantRange& range :
             interface_.pushConstantRanges) {
            for (const PushConstantMember& member : range.members) {
                const auto existing = std::find_if(
                    layout.fields.begin(), layout.fields.end(),
                    [&member](const PushConstantField& field) {
                        return field.qualifiedName == member.qualifiedName &&
                               field.offset == member.offset &&
                               field.size == member.size &&
                               field.type.scalar == member.type.scalar &&
                               field.type.rows == member.type.rows &&
                               field.type.columns == member.type.columns &&
                               field.arrayCount == member.arrayCount &&
                               field.arrayStride == member.arrayStride &&
                               field.matrixStride == member.matrixStride &&
                               field.rowMajor == member.rowMajor;
                    });
                if (existing != layout.fields.end()) {
                    continue;
                }
                layout.fields.push_back(
                    {member.name, member.qualifiedName, member.offset,
                     member.size, member.type, member.arrayCount,
                     member.arrayStride, member.matrixStride,
                     member.rowMajor});
            }
        }
        return layout;
    }

    ShaderModule* moduleFor(ShaderStage stage) noexcept {
        for (ShaderModule& module : stages_) {
            if (module.stage() == stage) {
                return &module;
            }
        }
        return nullptr;
    }

    const ShaderModule* moduleFor(ShaderStage stage) const noexcept {
        for (const ShaderModule& module : stages_) {
            if (module.stage() == stage) {
                return &module;
            }
        }
        return nullptr;
    }

    bool validate(std::string* error = nullptr) const {
        const auto fail = [error](const char* message) {
            if (error) {
                *error = message;
            }
            return false;
        };
        if (stages_.empty()) {
            return fail("Shader must contain at least one stage");
        }

        uint32_t stageMask = 0;
        for (const ShaderModule& module : stages_) {
            const uint32_t moduleStage =
                static_cast<uint32_t>(module.stage());
            if ((stageMask & moduleStage) != 0) {
                return fail("Shader contains a duplicate stage");
            }
            stageMask |= moduleStage;
            std::string moduleError;
            if (!module.validate(&moduleError)) {
                if (error) {
                    *error = "Invalid shader module: " + moduleError;
                }
                return false;
            }
        }

        const bool compute = hasShaderStage(
            static_cast<ShaderStage>(stageMask), ShaderStage::Compute);
        if (compute) {
            if (stages_.size() != 1) {
                return fail("Compute shaders cannot include graphics stages");
            }
        } else {
            if (!hasShaderStage(static_cast<ShaderStage>(stageMask),
                                ShaderStage::Vertex)) {
                return fail("Graphics shader requires a vertex stage");
            }
            const bool tessControl = hasShaderStage(
                static_cast<ShaderStage>(stageMask), ShaderStage::TessControl);
            const bool tessEval = hasShaderStage(
                static_cast<ShaderStage>(stageMask), ShaderStage::TessEval);
            if (tessControl != tessEval) {
                return fail(
                    "Tessellation control and evaluation stages must be paired");
            }
        }
        return interface_.validate(error);
    }

    uint64_t sizeBytes() const noexcept override {
        uint64_t bytes = sizeof(*this) + name_.size() +
                         stages_.capacity() * sizeof(ShaderModule);
        for (const ShaderModule& module : stages_) {
            bytes += module.sizeBytes() - sizeof(ShaderModule);
        }
        return bytes;
    }

private:
    std::string name_;
    std::vector<ShaderModule> stages_;
    ShaderInterface interface_;
};

using ShaderPtr = std::shared_ptr<Shader>;

}  // namespace klib::krender
