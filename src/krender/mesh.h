// include/renderer/Mesh.h
#pragma once
#include <krender/resource_base.h>
#include <vulkan/vulkan.h>
#include <utility>
#include <vector>

namespace klib::krender {

// 顶点属性布局：resolve 后绑定时要用
struct VertexAttribute {
    uint32_t location = 0;
    VkFormat format   = VK_FORMAT_R32G32B32_SFLOAT;
    uint32_t offset   = 0;
};
struct VertexLayout {
    uint32_t stride = 0;
    std::vector<VertexAttribute> attributes;
};

// 单 stream interleaved（多 stream 作为扩展，见末尾说明）
struct MeshData {
    const void*  vertices = nullptr;
    VkDeviceSize vertexBytes = 0;
    uint32_t     vertexCount = 0;
    VertexLayout layout;

    const void*  indices = nullptr;
    VkDeviceSize indexBytes = 0;
    uint32_t     indexCount = 0;
    VkIndexType  indexType = VK_INDEX_TYPE_UINT32;
};

class Mesh : public ResourceBase {
public:
    Mesh(Handle h, MeshData data) : ResourceBase(h), data_(std::move(data)) {}

    const MeshData& data() const { return data_; }
    uint64_t sizeBytes() const noexcept override {
        return data_.vertexBytes + data_.indexBytes;
    }

    // 同尺寸更新（粒子、动画顶点）：只 bump version，GPU 走 buffer 重写
    void updateVertices(const void* verts, VkDeviceSize bytes) {
        data_.vertices = verts;
        data_.vertexBytes = bytes;
        bumpVersion();
    }
    // 顶点数/索引数变 → 需要重新分配 buffer slot
    void rebuild(MeshData data) {
        data_ = std::move(data);
        ++sizeEpoch_;
        bumpVersion();
    }
    uint32_t sizeEpoch() const { return sizeEpoch_; }

private:
    MeshData data_;
    uint32_t sizeEpoch_ = 0;
};

} // namespace rnd