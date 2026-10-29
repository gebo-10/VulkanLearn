#pragma once
#include "resource_manager.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace klib::krender {

struct Vec2  { float x = 0, y = 0; };
struct Rect  { float x = 0, y = 0, w = 0, h = 0; };
struct Color { float r = 1, g = 1, b = 1, a = 1; };

enum class CmdType : uint16_t {
    Rect, Image, Mesh, Svg,
    PushTransform, PopTransform, SetClip,
};

struct DrawCmd {
    CmdType type;
    explicit DrawCmd(CmdType t) : type(t) {}
    virtual ~DrawCmd() = default;
};

struct DrawRectCmd : DrawCmd {
    Rect rect;
    Color color;
    DrawRectCmd() : DrawCmd(CmdType::Rect) {}
};

// 关键：ImageCmd 持有 {handle, version}，不持有 Image 指针
struct DrawImageCmd : DrawCmd {
    Handle   image;
    uint32_t version = 0;
    Rect     dst;              // 目标矩形（屏幕空间）
    Rect     srcUV;            // 逻辑 UV（渲染层会叠加图集重映射）
    DrawImageCmd() : DrawCmd(CmdType::Image) {}
};

struct DrawMeshCmd : DrawCmd {
    Handle   mesh;
    uint32_t version = 0;
    uint32_t indexCount = 0;
    Rect     transform;        // 简化：仅示意
    DrawMeshCmd() : DrawCmd(CmdType::Mesh) {}
};

using DrawCmdList = std::vector<std::unique_ptr<DrawCmd>>;

} // namespace klib::krender