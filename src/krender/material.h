#pragma once

#include "render_buffer.h"
#include "shader.h"
#include "texture.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace klib::krender {

using PropertyValue =
    std::variant<int32_t, uint32_t, float, std::array<float, 2>,
                 std::array<float, 3>, std::array<float, 4>,
                 std::array<float, 9>, std::array<float, 16>>;

// Keeps the typed value alongside its shader-layout byte representation.
struct Property {
    PropertyValue value;
    template <typename T>
    explicit Property(T&& v) : value(std::forward<T>(v)) {}
};

class Material : public ResourceBase {
public:
    explicit Material(Handle handle) : ResourceBase(handle) {}

    // ---------- Shader ----------
    void setShader(std::shared_ptr<Shader> shader) {
        if (!shader) {
            throw std::invalid_argument("Material requires a valid shader");
        }
        PushConstantLayout layout = shader->pushConstantLayout();
        std::vector<std::byte> propertyData(layout.size, std::byte{0});
        std::unordered_map<const PushConstantField*, std::string>
            propertyNamesByField;
        for (const auto& [name, property] : properties_) {
            const PushConstantField* field = layout.findField(name);
            if (field == nullptr) {
                continue;
            }
            const auto inserted = propertyNamesByField.emplace(field, name);
            if (!inserted.second) {
                throw std::invalid_argument(
                    "Multiple material properties refer to shader field: " +
                    field->propertyName);
            }
            if (field->offset > propertyData.size() ||
                field->size > propertyData.size() - field->offset) {
                throw std::out_of_range(
                    "Shader push-constant property exceeds its layout: " +
                    name);
            }
            writePropertyValue(*field, property.value,
                               propertyData.data() + field->offset);
        }
        for (const auto& [name, value] : rawProperties_) {
            const PushConstantField* field = layout.findField(name);
            if (field == nullptr) {
                continue;
            }
            const auto inserted = propertyNamesByField.emplace(field, name);
            if (!inserted.second) {
                throw std::invalid_argument(
                    "Multiple material properties refer to shader field: " +
                    field->propertyName);
            }
            if (field->offset > propertyData.size() ||
                field->size > propertyData.size() - field->offset) {
                throw std::out_of_range(
                    "Shader push-constant property exceeds its layout: " +
                    name);
            }
            writeRawPropertyValue(*field, value.data(), value.size(),
                                  propertyData.data() + field->offset);
        }
        propertyLayout_ = std::move(layout);
        propertyData_ = std::move(propertyData);
        shader_ = std::move(shader);
        bumpVersion();
    }

    void clearShader() {
        shader_.reset();
        bumpVersion();
        // Keep the last shader layout and property bytes available.
    }

    const std::shared_ptr<Shader>& shader() const noexcept { return shader_; }
    bool hasShader() const noexcept { return shader_ != nullptr; }

    // ---------- Texture / RenderBuffer（未改动，略） ----------
    void setTexture(std::string name, std::shared_ptr<Texture> texture) {
        if (name.empty()) throw std::invalid_argument("Texture binding name must not be empty");
        if (!texture) throw std::invalid_argument("Material requires a valid texture");
        textures_.insert_or_assign(std::move(name), std::move(texture));
        bumpVersion();
    }
    bool removeTexture(const std::string& name) {
        if (textures_.erase(name) == 0) return false;
        bumpVersion();
        return true;
    }
    const std::shared_ptr<Texture>* texture(const std::string& name) const noexcept {
        const auto it = textures_.find(name);
        return it == textures_.end() ? nullptr : &it->second;
    }
    const std::unordered_map<std::string, std::shared_ptr<Texture>>& textures() const noexcept {
        return textures_;
    }

    void setRenderBuffer(std::string name, std::shared_ptr<RenderBuffer> rb) {
        if (name.empty()) throw std::invalid_argument("Render buffer binding name must not be empty");
        if (!rb) throw std::invalid_argument("Material requires a valid render buffer");
        renderBuffers_.insert_or_assign(std::move(name), std::move(rb));
        bumpVersion();
    }
    bool removeRenderBuffer(const std::string& name) {
        if (renderBuffers_.erase(name) == 0) return false;
        bumpVersion();
        return true;
    }
    const std::shared_ptr<RenderBuffer>* renderBuffer(const std::string& name) const noexcept {
        const auto it = renderBuffers_.find(name);
        return it == renderBuffers_.end() ? nullptr : &it->second;
    }
    const std::unordered_map<std::string, std::shared_ptr<RenderBuffer>>& renderBuffers() const noexcept {
        return renderBuffers_;
    }

    // ---------- Property ----------
    template <typename T>
    void setProperty(std::string name, T&& value) {
        using ValueType = std::decay_t<T>;
        static_assert(std::is_constructible_v<PropertyValue, ValueType>,
                      "Unsupported material property type");
        if (name.empty()) {
            throw std::invalid_argument("Property name must not be empty");
        }

        PropertyValue propertyValue(std::forward<T>(value));
        const PushConstantField* field = propertyLayout_.findField(name);
        if (shader_ != nullptr && field == nullptr) {
            throw std::invalid_argument(
                "Shader has no push-constant property named: " + name);
        }
        std::vector<std::byte> encodedValue;
        if (field != nullptr) {
            if (field->offset > propertyData_.size() ||
                field->size > propertyData_.size() - field->offset) {
                throw std::out_of_range(
                    "Shader push-constant property exceeds its layout: " +
                    name);
            }
            encodedValue.resize(field->size, std::byte{0});
            writePropertyValue(*field, propertyValue,
                               encodedValue.data());
        }

        properties_.insert_or_assign(name, Property{propertyValue});
        if (field != nullptr) {
            for (auto it = rawProperties_.begin();
                 it != rawProperties_.end();) {
                if (propertyLayout_.findField(it->first) == field) {
                    it = rawProperties_.erase(it);
                } else {
                    ++it;
                }
            }
        } else {
            rawProperties_.erase(name);
        }
        if (field != nullptr) {
            std::memcpy(propertyData_.data() + field->offset,
                        encodedValue.data(), encodedValue.size());
        }
        bumpVersion();
    }

    // Raw values are tightly packed; matrices are repacked to shader layout.
    void setProperty(std::string name, const void* data, size_t size) {
        if (name.empty()) {
            throw std::invalid_argument("Property name must not be empty");
        }
        if (data == nullptr || size == 0) {
            throw std::invalid_argument(
                "Raw material property requires non-empty source data");
        }

        const PushConstantField* field = propertyLayout_.findField(name);
        if (shader_ != nullptr && field == nullptr) {
            throw std::invalid_argument(
                "Shader has no push-constant property named: " + name);
        }

        std::vector<std::byte> source(size);
        std::memcpy(source.data(), data, size);
        std::vector<std::byte> encodedValue;
        if (field != nullptr) {
            if (field->offset > propertyData_.size() ||
                field->size > propertyData_.size() - field->offset) {
                throw std::out_of_range(
                    "Shader push-constant property exceeds its layout: " +
                    name);
            }
            encodedValue.resize(field->size, std::byte{0});
            writeRawPropertyValue(*field, source.data(), source.size(),
                                  encodedValue.data());

            for (auto it = properties_.begin(); it != properties_.end();) {
                if (propertyLayout_.findField(it->first) == field) {
                    it = properties_.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto it = rawProperties_.begin();
                 it != rawProperties_.end();) {
                if (propertyLayout_.findField(it->first) == field) {
                    it = rawProperties_.erase(it);
                } else {
                    ++it;
                }
            }
        } else {
            properties_.erase(name);
            rawProperties_.erase(name);
        }

        rawProperties_.emplace(name, std::move(source));
        if (field != nullptr) {
            std::memcpy(propertyData_.data() + field->offset,
                        encodedValue.data(), encodedValue.size());
        }
        bumpVersion();
    }

    bool removeProperty(const std::string& name) {
        auto propertyIt = properties_.find(name);
        auto rawPropertyIt = rawProperties_.find(name);
        const PushConstantField* field = propertyLayout_.findField(name);
        if (propertyIt == properties_.end() &&
            rawPropertyIt == rawProperties_.end() && field != nullptr) {
            propertyIt = std::find_if(
                properties_.begin(), properties_.end(),
                [this, field](const auto& entry) {
                    return propertyLayout_.findField(entry.first) == field;
                });
            if (propertyIt == properties_.end()) {
                rawPropertyIt = std::find_if(
                    rawProperties_.begin(), rawProperties_.end(),
                    [this, field](const auto& entry) {
                        return propertyLayout_.findField(entry.first) == field;
                    });
            }
        }
        if (propertyIt == properties_.end() &&
            rawPropertyIt == rawProperties_.end()) {
            return false;
        }
        if (propertyIt != properties_.end()) {
            properties_.erase(propertyIt);
        } else {
            rawProperties_.erase(rawPropertyIt);
        }
        if (field != nullptr && propertyData_.size() >= field->offset &&
            field->size <= propertyData_.size() - field->offset) {
            const bool hasTypedAlias = std::any_of(
                properties_.begin(), properties_.end(),
                [this, field](const auto& entry) {
                    return propertyLayout_.findField(entry.first) == field;
                });
            const bool hasRawAlias = std::any_of(
                rawProperties_.begin(), rawProperties_.end(),
                [this, field](const auto& entry) {
                    return propertyLayout_.findField(entry.first) == field;
                });
            if (hasTypedAlias || hasRawAlias) {
                bumpVersion();
                return true;
            }
            std::memset(propertyData_.data() + field->offset, 0, field->size);
        }
        bumpVersion();
        return true;
    }

    const Property* property(const std::string& name) const noexcept {
        const auto it = properties_.find(name);
        if (it != properties_.end()) {
            return &it->second;
        }
        const PushConstantField* field = propertyLayout_.findField(name);
        if (field == nullptr) {
            return nullptr;
        }
        for (const auto& [propertyName, value] : properties_) {
            if (propertyLayout_.findField(propertyName) == field) {
                return &value;
            }
        }
        return nullptr;
    }

    template <typename T>
    const T* propertyValue(const std::string& name) const noexcept {
        const Property* found = property(name);
        return found ? std::get_if<T>(&found->value) : nullptr;
    }

    const std::byte* propertyRaw(const std::string& name) const noexcept {
        const PushConstantField* field = propertyLayout_.findField(name);
        if (field == nullptr || propertyData_.empty()) {
            return nullptr;
        }
        for (const auto& [propertyName, value] : properties_) {
            (void)value;
            if (propertyLayout_.findField(propertyName) == field) {
                return propertyData_.data() + field->offset;
            }
        }
        for (const auto& [propertyName, value] : rawProperties_) {
            (void)value;
            if (propertyLayout_.findField(propertyName) == field) {
                return propertyData_.data() + field->offset;
            }
        }
        return nullptr;
    }

    const std::vector<std::byte>& propertyData() const noexcept {
        return propertyData_;
    }

    std::vector<std::byte> packPushConstants(
        const PushConstantLayout& layout) const {
        std::vector<std::byte> data(layout.size, std::byte{0});
        for (const PushConstantField& field : layout.fields) {
            const std::string& fieldName =
                field.qualifiedName.empty() ? field.propertyName
                                            : field.qualifiedName;
            const std::byte* source = propertyRaw(fieldName);
            if (source == nullptr) {
                throw std::invalid_argument(
                    "Missing material property: " + fieldName);
            }
            if (field.offset > layout.size ||
                field.size > layout.size - field.offset) {
                throw std::out_of_range(
                    "Push constant property exceeds the provided layout");
            }
            std::memcpy(data.data() + field.offset,
                        source, field.size);
        }
        return data;
    }

    uint64_t sizeBytes() const noexcept override {
        uint64_t bytes = sizeof(*this);
        bytes += propertyData_.capacity();
        for (const auto& [name, property] : properties_) {
            (void)property;
            bytes += name.size();
            bytes += sizeof(Property);
        }
        for (const auto& [name, value] : rawProperties_) {
            bytes += name.size() + value.capacity();
        }
        for (const auto& [name, t] : textures_) {
            (void)t;
            bytes += name.size() + sizeof(std::shared_ptr<Texture>);
        }
        for (const auto& [name, rb] : renderBuffers_) {
            (void)rb;
            bytes += name.size() + sizeof(std::shared_ptr<RenderBuffer>);
        }
        bytes += sizeof(std::shared_ptr<Shader>);
        return bytes;
    }

private:
    static void writeRawPropertyValue(const PushConstantField& field,
                                      const void* source, size_t sourceSize,
                                      std::byte* destination) {
        if (field.arrayCount != 1) {
            throw std::invalid_argument(
                "Push-constant arrays are not supported for property: " +
                field.propertyName);
        }
        if (field.size == 0 || source == nullptr || destination == nullptr) {
            throw std::invalid_argument(
                "Raw push-constant property has invalid storage");
        }

        size_t scalarSize = 0;
        switch (field.type.scalar) {
            case ShaderScalarType::Bool:
            case ShaderScalarType::Int32:
            case ShaderScalarType::UInt32:
            case ShaderScalarType::Float32:
                scalarSize = sizeof(uint32_t);
                break;
            case ShaderScalarType::Float64:
                scalarSize = sizeof(double);
                break;
            default:
                throw std::invalid_argument(
                    "Raw push-constant property has an unsupported scalar type: " +
                    field.propertyName);
        }
        const size_t valueCount =
            static_cast<size_t>(field.type.rows) * field.type.columns;
        if (valueCount > std::numeric_limits<size_t>::max() / scalarSize ||
            sourceSize != valueCount * scalarSize) {
            throw std::invalid_argument(
                "Raw material property size does not match shader field: " +
                field.propertyName);
        }

        std::memset(destination, 0, field.size);
        if (field.type.rows > 1 && field.type.columns > 1) {
            const uint32_t majorCount =
                field.rowMajor ? field.type.rows : field.type.columns;
            const uint32_t minorCount =
                field.rowMajor ? field.type.columns : field.type.rows;
            const uint32_t stride =
                field.matrixStride != 0
                    ? field.matrixStride
                    : minorCount * static_cast<uint32_t>(scalarSize);
            const auto* sourceBytes =
                static_cast<const std::byte*>(source);
            for (uint32_t major = 0; major < majorCount; ++major) {
                for (uint32_t minor = 0; minor < minorCount; ++minor) {
                    const uint32_t row =
                        field.rowMajor ? major : minor;
                    const uint32_t column =
                        field.rowMajor ? minor : major;
                    const size_t sourceIndex =
                        static_cast<size_t>(column) * field.type.rows + row;
                    const size_t destinationOffset =
                        static_cast<size_t>(major) * stride +
                        static_cast<size_t>(minor) * scalarSize;
                    if (destinationOffset + scalarSize > field.size) {
                        throw std::invalid_argument(
                            "Reflected matrix stride exceeds property size: " +
                            field.propertyName);
                    }
                    std::memcpy(destination + destinationOffset,
                                sourceBytes + sourceIndex * scalarSize,
                                scalarSize);
                }
            }
            return;
        }

        if (sourceSize > field.size) {
            throw std::invalid_argument(
                "Raw material property exceeds shader field size: " +
                field.propertyName);
        }
        std::memcpy(destination, source, sourceSize);
    }

    static void writePropertyValue(const PushConstantField& field,
                                   const PropertyValue& value,
                                   std::byte* destination) {
        if (field.arrayCount != 1) {
            throw std::invalid_argument(
                "Push-constant arrays are not supported for property: " +
                field.propertyName);
        }
        if (field.size == 0 || destination == nullptr) {
            throw std::invalid_argument(
                "Push-constant property has an invalid reflected size");
        }
        std::memset(destination, 0, field.size);

        const uint32_t dimensions =
            static_cast<uint32_t>(field.type.rows) * field.type.columns;
        const auto failType = [&field]() {
            throw std::invalid_argument(
                "Material property type does not match shader field: " +
                field.propertyName);
        };
        if (dimensions == 1) {
            const void* source = nullptr;
            size_t sourceSize = 0;
            switch (field.type.scalar) {
                case ShaderScalarType::Int32:
                    if (const auto* v = std::get_if<int32_t>(&value)) {
                        source = v;
                        sourceSize = sizeof(*v);
                    }
                    break;
                case ShaderScalarType::UInt32:
                    if (const auto* v = std::get_if<uint32_t>(&value)) {
                        source = v;
                        sourceSize = sizeof(*v);
                    }
                    break;
                case ShaderScalarType::Float32:
                    if (const auto* v = std::get_if<float>(&value)) {
                        source = v;
                        sourceSize = sizeof(*v);
                    }
                    break;
                default:
                    break;
            }
            if (source == nullptr || sourceSize > field.size) {
                failType();
            }
            std::memcpy(destination, source, sourceSize);
            return;
        }

        if (field.type.scalar != ShaderScalarType::Float32) {
            failType();
        }
        if (field.type.rows > 1 && field.type.columns > 1) {
            const float* values = nullptr;
            size_t valueCount = 0;
            if (const auto* v = std::get_if<std::array<float, 4>>(&value)) {
                values = v->data();
                valueCount = v->size();
            } else if (const auto* v =
                           std::get_if<std::array<float, 9>>(&value)) {
                values = v->data();
                valueCount = v->size();
            } else if (const auto* v =
                           std::get_if<std::array<float, 16>>(&value)) {
                values = v->data();
                valueCount = v->size();
            }
            const size_t expectedCount =
                static_cast<size_t>(field.type.rows) * field.type.columns;
            if (values == nullptr || valueCount != expectedCount) {
                failType();
            }
            const uint32_t majorCount =
                field.rowMajor ? field.type.rows : field.type.columns;
            const uint32_t minorCount =
                field.rowMajor ? field.type.columns : field.type.rows;
            const uint32_t stride =
                field.matrixStride != 0
                    ? field.matrixStride
                    : minorCount * static_cast<uint32_t>(sizeof(float));
            for (uint32_t major = 0; major < majorCount; ++major) {
                for (uint32_t minor = 0; minor < minorCount; ++minor) {
                    const uint32_t row =
                        field.rowMajor ? major : minor;
                    const uint32_t column =
                        field.rowMajor ? minor : major;
                    const size_t sourceIndex =
                        static_cast<size_t>(column) * field.type.rows + row;
                    const size_t destinationOffset =
                        static_cast<size_t>(major) * stride +
                        static_cast<size_t>(minor) * sizeof(float);
                    if (destinationOffset + sizeof(float) > field.size) {
                        throw std::invalid_argument(
                            "Reflected matrix stride exceeds property size: " +
                            field.propertyName);
                    }
                    std::memcpy(destination + destinationOffset,
                                values + sourceIndex, sizeof(float));
                }
            }
            return;
        }

        const float* values = nullptr;
        size_t valueCount = 0;
        if (const auto* v = std::get_if<std::array<float, 2>>(&value)) {
            values = v->data();
            valueCount = v->size();
        } else if (const auto* v =
                       std::get_if<std::array<float, 3>>(&value)) {
            values = v->data();
            valueCount = v->size();
        } else if (const auto* v =
                       std::get_if<std::array<float, 4>>(&value)) {
            values = v->data();
            valueCount = v->size();
        }
        if (values == nullptr || valueCount != dimensions ||
            valueCount * sizeof(float) > field.size) {
            failType();
        }
        std::memcpy(destination, values, valueCount * sizeof(float));
    }

    std::shared_ptr<Shader> shader_;
    std::unordered_map<std::string, std::shared_ptr<Texture>> textures_;
    std::unordered_map<std::string, std::shared_ptr<RenderBuffer>> renderBuffers_;

    PushConstantLayout propertyLayout_;
    std::vector<std::byte> propertyData_;
    std::unordered_map<std::string, Property> properties_;
    std::unordered_map<std::string, std::vector<std::byte>> rawProperties_;
};

using MaterialPtr = std::shared_ptr<Material>;

template <>
struct ResourceTraits<Material> {
    static constexpr ZeroRefPolicy zeroRef = ZeroRefPolicy::Immediate;
    static constexpr ResourceKind kind = ResourceKind::Material;
};

}  // namespace klib::krender