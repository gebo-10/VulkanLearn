#pragma once

#include "resource_manager.h"
#include "shader.h"
#include "texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

struct Property {
    PropertyValue value;

    template <typename T>
    explicit Property(T&& propertyValue)
        : value(std::forward<T>(propertyValue)) {}
};

struct PushConstantField {
    std::string propertyName;
    uint32_t offset = 0;
};

struct PushConstantLayout {
    uint32_t size = 0;
    std::vector<PushConstantField> fields;
};

class Material : public ResourceBase {
public:
    explicit Material(Handle handle) : ResourceBase(handle) {}

    void setShader(std::shared_ptr<Shader> shader) {
        if (!shader) {
            throw std::invalid_argument("Material requires a valid shader");
        }
        shader_ = std::move(shader);
        bumpVersion();
    }

    void clearShader() {
        shader_.reset();
        bumpVersion();
    }

    const std::shared_ptr<Shader>& shader() const noexcept { return shader_; }
    bool hasShader() const noexcept { return shader_ != nullptr; }

    void setTexture(std::string name, std::shared_ptr<Texture> texture) {
        if (name.empty()) {
            throw std::invalid_argument("Texture binding name must not be empty");
        }
        if (!texture) {
            throw std::invalid_argument("Material requires a valid texture");
        }
        textures_.insert_or_assign(std::move(name), std::move(texture));
        bumpVersion();
    }

    bool removeTexture(const std::string& name) {
        if (textures_.erase(name) == 0) {
            return false;
        }
        bumpVersion();
        return true;
    }

    const std::shared_ptr<Texture>* texture(
        const std::string& name) const noexcept {
        const auto it = textures_.find(name);
        return it == textures_.end() ? nullptr : &it->second;
    }

    const std::unordered_map<std::string, std::shared_ptr<Texture>>&
    textures() const noexcept {
        return textures_;
    }

    template <typename T>
    void setProperty(std::string name, T&& value) {
        using ValueType = std::decay_t<T>;
        static_assert(
            std::is_constructible_v<PropertyValue, ValueType>,
            "Unsupported material property type");
        if (name.empty()) {
            throw std::invalid_argument("Property name must not be empty");
        }
        properties_.insert_or_assign(
            std::move(name),
            Property{PropertyValue(std::forward<T>(value))});
        bumpVersion();
    }

    bool removeProperty(const std::string& name) {
        if (properties_.erase(name) == 0) {
            return false;
        }
        bumpVersion();
        return true;
    }

    const Property* property(const std::string& name) const noexcept {
        const auto it = properties_.find(name);
        return it == properties_.end() ? nullptr : &it->second;
    }

    template <typename T>
    const T* propertyValue(const std::string& name) const noexcept {
        const Property* found = property(name);
        return found ? std::get_if<T>(&found->value) : nullptr;
    }

    const std::unordered_map<std::string, Property>& properties() const noexcept {
        return properties_;
    }

    std::vector<std::byte> packPushConstants(
        const PushConstantLayout& layout) const {
        std::vector<std::byte> data(layout.size, std::byte{0});
        for (const PushConstantField& field : layout.fields) {
            const auto it = properties_.find(field.propertyName);
            if (it == properties_.end()) {
                throw std::invalid_argument(
                    "Missing material property: " + field.propertyName);
            }

            const PropertyValue& value = it->second.value;
            const size_t valueSize = std::visit(
                [](const auto& propertyValue) { return sizeof(propertyValue); },
                value);
            if (field.offset > layout.size ||
                valueSize > layout.size - field.offset) {
                throw std::out_of_range(
                    "Push constant property exceeds the provided layout");
            }

            std::visit(
                [&data, &field](const auto& propertyValue) {
                    std::memcpy(data.data() + field.offset, &propertyValue,
                                sizeof(propertyValue));
                },
                value);
        }
        return data;
    }

    uint64_t sizeBytes() const noexcept override {
        uint64_t bytes = sizeof(*this);
        for (const auto& [name, propertyValue] : properties_) {
            bytes += name.size();
            bytes += std::visit(
                [](const auto& value) {
                    return static_cast<uint64_t>(sizeof(value));
                },
                propertyValue.value);
        }
        for (const auto& [name, texture] : textures_) {
            (void)texture;
            bytes += name.size() + sizeof(std::shared_ptr<Texture>);
        }
        bytes += sizeof(std::shared_ptr<Shader>);
        return bytes;
    }

private:
    std::shared_ptr<Shader> shader_;
    std::unordered_map<std::string, std::shared_ptr<Texture>> textures_;
    std::unordered_map<std::string, Property> properties_;
};

using MaterialPtr = std::shared_ptr<Material>;

template <>
struct ResourceTraits<Material> {
    static constexpr ZeroRefPolicy zeroRef = ZeroRefPolicy::Immediate;
    static constexpr ResourceKind kind = ResourceKind::Material;
};

}  // namespace klib::krender
