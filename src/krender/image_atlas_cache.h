// ImageAtlasCache.h
#pragma once
#include "eviction_policy.h"
#include "resource_manager.h"
#include "vulkan_context.h"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace klib::krender {

struct ImageData {
    const uint8_t* pixels = nullptr;
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t bytesPerRow = 0;
};

// 图集内矩形（含 UV 重映射辅助）
struct AtlasRect {
    int32_t x = 0, y = 0;
    int32_t w = 0, h = 0;
    bool valid() const { return w > 0 && h > 0; }
    float u0(uint32_t aw) const { return static_cast<float>(x) / aw; }
    float v0(uint32_t ah) const { return static_cast<float>(y) / ah; }
    float u1(uint32_t aw) const { return static_cast<float>(x + w) / aw; }
    float v1(uint32_t ah) const { return static_cast<float>(y + h) / ah; }
};

// 单张图集：shelf 打包 + 上传 + 碎片整理
class Atlas {
public:
    Atlas(VulkanContext* ctx, uint32_t w, uint32_t h);
    ~Atlas();

    bool     allocate(uint32_t w, uint32_t h, AtlasRect& out);
    void     free(const AtlasRect& r);
    void     upload(VkCommandBuffer cmd, const AtlasRect& r,
                    const ImageData& data);
    // 碎片率超过阈值时重建（把存活矩形紧凑化到新图集）
    bool     needsCompaction() const;
    uint64_t usedBytes() const { return usedBytes_; }

    VkImage     image() const { return image_; }
    VkImageView view()  const { return view_; }
    uint32_t    width() const { return width_; }
    uint32_t    height() const { return height_; }

    // 供 compaction 遍历
    struct UsedEntry { AtlasRect rect; Handle owner; };
    const std::vector<UsedEntry>& used() const { return used_; }

private:
    struct Shelf { int32_t y; int32_t height; int32_t usedWidth; };
    VulkanContext* ctx_;
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    uint32_t width_ = 0, height_ = 0;
    uint64_t usedBytes_ = 0;
    int32_t nextShelfY_ = 0;
    std::vector<Shelf> shelves_;
    std::vector<UsedEntry> used_;
    std::vector<AtlasRect> freeList_;
};

struct ImageAtlasEntry {
    Handle   handle;          // 完整 handle（含 generation）
    uint32_t version = 0;
    uint32_t atlasIndex = 0;
    AtlasRect rect;
    uint32_t width = 0, height = 0;
    uint64_t lastUsedFrame = 0;
    uint32_t accessCount = 0;
    bool     pinned = false;
    bool     alive  = false;
};

class ImageAtlasCache {
public:
    ImageAtlasCache(VulkanContext* ctx,
                    std::unique_ptr<IEvictionPolicy> policy,
                    uint64_t budgetBytes = 64ull * 1024 * 1024,
                    uint32_t atlasSize = 2048);
    ~ImageAtlasCache();

    // 上传/更新（内容版本变化时调用）
    void insertOrUpdate(Handle h, uint32_t version, const ImageData& data);

    // 帧准备：扫描命令，pin 住本帧要用的资源；返回待上传列表
    void prepare(const std::vector<std::pair<Handle, uint32_t>>& required,
                 uint64_t frameIndex);

    // 刷新上传队列
    void flushUploads(VkCommandBuffer cmd);

    // 帧结束：解除 pin、执行驱逐
    void endFrame(uint64_t frameIndex);

    // 解析：输出视图 + UV 矩形
    bool resolve(Handle h, uint32_t version,
                 VkImageView& outView, AtlasRect& outRect) const;

    uint64_t usedBytes() const;

private:
    Atlas* acquireAtlas(uint32_t w, uint32_t h, AtlasRect& outRect,
                        uint32_t& outAtlasIndex);
    void   evictIfNeeded();
    void   compactAtlas(uint32_t atlasIndex);

    VulkanContext* ctx_;
    std::unique_ptr<IEvictionPolicy> policy_;
    uint64_t budgetBytes_;
    uint32_t atlasSize_;
    std::vector<std::unique_ptr<Atlas>> atlases_;
    std::vector<ImageAtlasEntry> entries_;   // index = handle.index
    std::vector<std::pair<Handle, uint32_t>> pendingUploads_; // handle, version
    uint64_t frameIndex_ = 0;
};

} // namespace klib::krender


namespace klib::krender {

// ---------------- Atlas ----------------
inline Atlas::Atlas(VulkanContext* ctx, uint32_t w, uint32_t h)
    : ctx_(ctx), width_(w), height_(h) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(ctx_->device, &ci, nullptr, &image_);

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(ctx_->device, image_, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = ctx_->findMemoryType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(ctx_->device, &ai, nullptr, &memory_);
    vkBindImageMemory(ctx_->device, image_, memory_, 0);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = image_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(ctx_->device, &vi, nullptr, &view_);
}

inline Atlas::~Atlas() {
    vkDestroyImageView(ctx_->device, view_, nullptr);
    vkDestroyImage(ctx_->device, image_, nullptr);
    vkFreeMemory(ctx_->device, memory_, nullptr);
}

inline bool Atlas::allocate(uint32_t w, uint32_t h, AtlasRect& out) {
    // 1. 优先复用 free list 中足够大的矩形
    for (auto it = freeList_.begin(); it != freeList_.end(); ++it) {
        if (it->w >= static_cast<int32_t>(w) && it->h >= static_cast<int32_t>(h)) {
            out = { it->x, it->y, static_cast<int32_t>(w), static_cast<int32_t>(h) };
            it->y += h; it->h -= h;
            if (it->h == 0) freeList_.erase(it);
            usedBytes_ += static_cast<uint64_t>(w) * h * 4;
            used_.push_back({out, {}});
            return true;
        }
    }
    // 2. shelf
    for (auto& s : shelves_) {
        if (s.height >= static_cast<int32_t>(h) &&
            s.usedWidth + static_cast<int32_t>(w) <= static_cast<int32_t>(width_)) {
            out = { s.usedWidth, s.y, static_cast<int32_t>(w), static_cast<int32_t>(h) };
            s.usedWidth += w;
            usedBytes_ += static_cast<uint64_t>(w) * h * 4;
            used_.push_back({out, {}});
            return true;
        }
    }
    // 3. 新 shelf
    if (nextShelfY_ + static_cast<int32_t>(h) > static_cast<int32_t>(height_))
        return false;
    shelves_.push_back({ nextShelfY_, static_cast<int32_t>(h), static_cast<int32_t>(w) });
    out = { 0, nextShelfY_, static_cast<int32_t>(w), static_cast<int32_t>(h) };
    nextShelfY_ += h;
    usedBytes_ += static_cast<uint64_t>(w) * h * 4;
    used_.push_back({out, {}});
    return true;
}

inline void Atlas::free(const AtlasRect& r) {
    freeList_.push_back(r);
    usedBytes_ -= static_cast<uint64_t>(r.w) * r.h * 4;
    used_.erase(std::remove_if(used_.begin(), used_.end(),
        [&](const UsedEntry& e) {
            return e.rect.x == r.x && e.rect.y == r.y;
        }), used_.end());
}

inline void Atlas::upload(VkCommandBuffer /*cmd*/, const AtlasRect& r, const ImageData& data) {
    // 简化：此处假设调用方已开启一次性 cmd，只用 ctx->uploadToImage 会重建 barrier，
    // 生产实现应使用持久 staging ring 并复用当前 frame 的 cmd。
    VkDeviceMemory mem;
    VkBuffer staging;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = data.bytesPerRow * data.height;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    vkCreateBuffer(ctx_->device, &bci, nullptr, &staging);
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(ctx_->device, staging, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = ctx_->findMemoryType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(ctx_->device, &ai, nullptr, &mem);
    vkBindBufferMemory(ctx_->device, staging, mem, 0);
    void* ptr = nullptr;
    vkMapMemory(ctx_->device, mem, 0, bci.size, 0, &ptr);
    std::memcpy(ptr, data.pixels, bci.size);
    vkUnmapMemory(ctx_->device, mem);

    VkCommandBuffer cmd = ctx_->beginOneShot();
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {r.x, r.y, 0};
    region.imageExtent = {static_cast<uint32_t>(r.w), static_cast<uint32_t>(r.h), 1};
    vkCmdCopyBufferToImage(cmd, staging, image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    ctx_->endOneShot(cmd);

    vkDestroyBuffer(ctx_->device, staging, nullptr);
    vkFreeMemory(ctx_->device, mem, nullptr);
}

inline bool Atlas::needsCompaction() const {
    // 简化：空闲矩形总可用率低于 30% 时触发
    uint64_t freeBytes = 0;
    for (auto& r : freeList_) freeBytes += static_cast<uint64_t>(r.w) * r.h * 4;
    uint64_t total = static_cast<uint64_t>(width_) * height_ * 4;
    return total > 0 && (freeBytes * 10 / total) < 3;
}

// ---------------- ImageAtlasCache ----------------
inline ImageAtlasCache::ImageAtlasCache(VulkanContext* ctx,
                                 std::unique_ptr<IEvictionPolicy> policy,
                                 uint64_t budgetBytes, uint32_t atlasSize)
    : ctx_(ctx), policy_(std::move(policy)),
      budgetBytes_(budgetBytes), atlasSize_(atlasSize) {}

inline ImageAtlasCache::~ImageAtlasCache() = default;

inline Atlas* ImageAtlasCache::acquireAtlas(uint32_t w, uint32_t h, AtlasRect& outRect,
                                     uint32_t& outAtlasIndex) {
    for (size_t i = 0; i < atlases_.size(); ++i) {
        if (atlases_[i]->allocate(w, h, outRect)) {
            outAtlasIndex = static_cast<uint32_t>(i);
            return atlases_[i].get();
        }
    }
    // 尝试驱逐
    evictIfNeeded();
    if (usedBytes() + static_cast<uint64_t>(w) * h * 4 > budgetBytes_)
        return nullptr;
    atlases_.push_back(std::make_unique<Atlas>(ctx_, atlasSize_, atlasSize_));
    outAtlasIndex = static_cast<uint32_t>(atlases_.size() - 1);
    atlases_.back()->allocate(w, h, outRect);
    return atlases_.back().get();
}

inline void ImageAtlasCache::insertOrUpdate(Handle h, uint32_t version, const ImageData& data) {
    if (h.index >= entries_.size())
        entries_.resize(h.index + 1);

    auto& e = entries_[h.index];
    if (e.handle == h) {
        // 已有条目：version 变了 → 待重传
        if (e.version != version) {
            e.version = version;
            pendingUploads_.emplace_back(h, version);
        }
        return;
    }

    // 新条目：分配图集矩形
    AtlasRect rect;
    uint32_t atlasIdx = 0;
    Atlas* atlas = acquireAtlas(data.width, data.height, rect, atlasIdx);
    if (!atlas) {
        // 预算耗尽且无可驱逐 → 丢弃（生产可回退到占位纹理）
        return;
    }
    e.handle = h;
    e.version = version;
    e.atlasIndex = atlasIdx;
    e.rect = rect;
    e.width = data.width;
    e.height = data.height;
    e.alive = true;
    e.lastUsedFrame = frameIndex_;
    e.accessCount = 0;
    e.pinned = false;
    policy_->onInsert(h, static_cast<uint64_t>(data.width) * data.height * 4, frameIndex_);
    pendingUploads_.emplace_back(h, version);
}

inline void ImageAtlasCache::prepare(
    const std::vector<std::pair<Handle, uint32_t>>& required, uint64_t frameIndex) {
    frameIndex_ = frameIndex;
    // 1. 全部解除旧 pin
    for (auto& e : entries_) e.pinned = false;
    // 2. pin 本帧引用
    for (auto& [h, ver] : required) {
        if (h.index >= entries_.size()) continue;
        auto& e = entries_[h.index];
        if (e.handle == h && e.alive) {
            e.pinned = true;
            if (e.version != ver) {
                // 需要重传（同尺寸走原地 subupload）
                e.version = ver;
                pendingUploads_.emplace_back(h, ver);
            }
        }
    }
}

inline void ImageAtlasCache::flushUploads(VkCommandBuffer /*cmd*/) {
    // 生产实现：把 pendingUploads_ 中的 staging 数据按 atlas 合并成一次 cmd。
    // 这里为演示，逐条通过 atlas->upload 立即上传。
    for (auto& [h, ver] : pendingUploads_) {
        if (h.index >= entries_.size()) continue;
        auto& e = entries_[h.index];
        if (!e.alive) continue;
        // 实际需从 CPU 资源拿到 ImageData；此处示意调用点
        // atlases_[e.atlasIndex]->upload(cmd, e.rect, *cpuData);
    }
    pendingUploads_.clear();
}

inline void ImageAtlasCache::endFrame(uint64_t frameIndex) {
    frameIndex_ = frameIndex;
    // 解除 pin
    for (auto& e : entries_) e.pinned = false;
    // 尝试驱逐
    evictIfNeeded();
    policy_->onFrameEnd(frameIndex_);
}

inline bool ImageAtlasCache::resolve(Handle h, uint32_t version,
                              VkImageView& outView, AtlasRect& outRect) const {
    if (h.index >= entries_.size()) return false;
    const auto& e = entries_[h.index];
    if (e.handle != h || !e.alive) return false;
    if (e.version != version) return false;
    outView = atlases_[e.atlasIndex]->view();
    outRect = e.rect;
    return true;
}

inline uint64_t ImageAtlasCache::usedBytes() const {
    uint64_t sum = 0;
    for (auto& a : atlases_) sum += a->usedBytes();
    return sum;
}

inline void ImageAtlasCache::evictIfNeeded() {
    uint64_t used = usedBytes();
    if (used <= budgetBytes_) return;

    std::vector<EvictionCandidate> cands;
    cands.reserve(entries_.size());
    for (auto& e : entries_) {
        if (!e.alive) continue;
        cands.push_back({ e.handle, e.version, e.lastUsedFrame,
            static_cast<uint64_t>(e.width) * e.height * 4,
            e.accessCount, e.pinned });
    }
    auto victims = policy_->selectVictims(cands, used - budgetBytes_);
    for (size_t idx : victims) {
        Handle h = cands[idx].handle;
        if (h.index < entries_.size()) {
            auto& e = entries_[h.index];
            if (e.alive && !e.pinned) {
                atlases_[e.atlasIndex]->free(e.rect);
                e.alive = false;
                e.handle = {};
            }
        }
    }
}

inline void ImageAtlasCache::compactAtlas(uint32_t) { /* 生产：重建图集并重映射 UV */ }

} // namespace klib::krender