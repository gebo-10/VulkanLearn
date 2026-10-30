// 渲染接口抽象类，定义渲染器的基本操作
// 该接口可以被不同的渲染器实现，以支持不同的渲染后端（如 Vulkan、OpenGL 等）
// pipeline 根据material的shader 创建
// constant 根据material的shader interface 创建和material的property值上传

#pragma once

#include <krender/handle.h>
#include <glm/glm.hpp>
namespace klib::krender{

enum class ClearFlag : uint32_t {
    None    = 0,
    Color   = 1u << 0,
    Depth   = 1u << 1,
    Stencil = 1u << 2,
    All     = Color | Depth | Stencil,
};
constexpr ClearFlag operator|(ClearFlag a, ClearFlag b) noexcept {
    return static_cast<ClearFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr bool hasFlag(ClearFlag value, ClearFlag bit) noexcept {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(bit)) != 0;
}

struct ClearValue {
    glm::vec4 color{0.0f, 0.0f, 0.0f, 1.0f};
    float     depth   = 1.0f;
    uint32_t  stencil = 0;
};

class IRender
{
public:
    virtual ~IRender() = default;

    IRender() = default;
    IRender(const IRender&)            = delete;
    IRender& operator=(const IRender&) = delete;
    IRender(IRender&&)                 = delete;
    IRender& operator=(IRender&&)      = delete;


    // ---- 帧边界 ----
    virtual void beginFrame() = 0;
    virtual void endFrame()   = 0;

    virtual void clear(ClearFlag flags, const ClearValue& value) = 0;

    virtual void setViewport(const glm::vec4& viewport) = 0;
    virtual void setScissor(const glm::ivec4& scissor) = 0;
    virtual void setViewProjection(const glm::mat4& view, const glm::mat4& projection) = 0;
    virtual void renderMesh(const Handle& mesh, const Handle& material, const glm::mat4& modelMatrix, int instanceCount) = 0;
};
}