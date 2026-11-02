// GpuResourceCache.h
#pragma once
#include "draw_cmd.h"
#include "image_atlas_cache.h"
#include "vertex_buffer_cache.h"
#include <memory>
#include <utility>
#include <vector>

namespace klib::krender {

struct ResolvedImage  { VkImageView view; AtlasRect rect; bool ok = false; };
struct ResolvedMesh   { VkBuffer buffer; VkDeviceSize offset; bool ok = false; };

class GpuResourceCache {
public:
    GpuResourceCache(VulkanContext* ctx,
                     uint64_t imageBudget, uint64_t vertexBudget);

    // 帧准备：扫描 DrawCmd，收集依赖，pin 住
    void prepare(const DrawCmdList& cmds, uint64_t frameIndex);

    // 帧内上传（begin/endOneShot 由调用方管理，或内部合并）
    void flushUploads(VkCommandBuffer cmd);

    // 帧结束：解除 pin + 驱逐
    void endFrame(uint64_t frameIndex);

    // 解析（提交阶段使用）
    ResolvedImage resolveImage(Handle h, uint32_t version) const;
    ResolvedMesh  resolveMesh(Handle h, uint32_t version) const;

    // 资源侧更新入口
    void updateImage(Handle h, uint32_t version, const ImageData& data);
    void updateMesh(Handle h, uint32_t version, const VertexData& data);

private:
    ImageAtlasCache  images_;
    VertexBufferCache vertices_;
};

} // namespace klib::krender


namespace klib::krender {

inline GpuResourceCache::GpuResourceCache(VulkanContext* ctx,
                                   uint64_t imageBudget, uint64_t vertexBudget)
    : images_(ctx, std::make_unique<LruEvictionPolicy>(), imageBudget),
      vertices_(ctx, std::make_unique<LruEvictionPolicy>(), vertexBudget) {}

inline void GpuResourceCache::prepare(const DrawCmdList& cmds, uint64_t frameIndex) {
    std::vector<std::pair<Handle, uint32_t>> imgs, meshes;
    for (auto& c : cmds) {
        switch (c->type) {
        case CmdType::Image: {
            auto* ic = static_cast<ImageCmd*>(c.get());
            imgs.emplace_back(ic->image, ic->version);
            break;
        }
        case CmdType::Mesh: {
            auto* mc = static_cast<MeshCmd*>(c.get());
            meshes.emplace_back(mc->mesh, mc->version);
            break;
        }
        default: break;
        }
    }
    images_.prepare(imgs, frameIndex);
    vertices_.prepare(meshes, frameIndex);
}

inline void GpuResourceCache::flushUploads(VkCommandBuffer cmd) {
    images_.flushUploads(cmd);
    vertices_.flushUploads(cmd);
}

inline void GpuResourceCache::endFrame(uint64_t frameIndex) {
    images_.endFrame(frameIndex);
    vertices_.endFrame(frameIndex);
}

inline ResolvedImage GpuResourceCache::resolveImage(Handle h, uint32_t version) const {
    ResolvedImage r;
    r.ok = images_.resolve(h, version, r.view, r.rect);
    return r;
}

inline ResolvedMesh GpuResourceCache::resolveMesh(Handle h, uint32_t version) const {
    ResolvedMesh r;
    r.ok = vertices_.resolve(h, version, r.buffer, r.offset);
    return r;
}

inline void GpuResourceCache::updateImage(Handle h, uint32_t version, const ImageData& data) {
    images_.insertOrUpdate(h, version, data);
}

inline void GpuResourceCache::updateMesh(Handle h, uint32_t version, const VertexData& data) {
    vertices_.insertOrUpdate(h, version, data);
}

} // namespace klib::krender