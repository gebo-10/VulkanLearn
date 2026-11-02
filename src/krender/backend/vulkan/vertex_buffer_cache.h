// VertexBufferCache.h
#pragma once
#include "eviction_policy.h"
#include "resource_manager.h"
#include "vulkan_context.h"
#include <vulkan/vulkan.h>
#include <cstring>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace klib::krender {

struct VertexData {
    const void* data = nullptr;
    VkDeviceSize size = 0;
};

struct BufferAlloc {
    uint32_t bufferIndex = 0;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
};

struct VertexEntry {
    Handle   handle;
    uint32_t version = 0;
    BufferAlloc alloc;
    uint64_t lastUsedFrame = 0;
    uint32_t accessCount = 0;
    bool     pinned = false;
    bool     alive = false;
};

// 一个 bucket = 一个大的 VkBuffer，切成固定 size 的 slot
struct BufferBucket {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize slotSize = 0;
    uint32_t slotCount = 0;
    std::vector<uint32_t> freeSlots;
};

class VertexBufferCache {
public:
    VertexBufferCache(VulkanContext* ctx,
                      std::unique_ptr<IEvictionPolicy> policy,
                      uint64_t budgetBytes = 256ull * 1024 * 1024);
    ~VertexBufferCache();

    void insertOrUpdate(Handle h, uint32_t version, const VertexData& data);
    void prepare(const std::vector<std::pair<Handle, uint32_t>>& required,
                 uint64_t frameIndex);
    void flushUploads(VkCommandBuffer cmd);
    void endFrame(uint64_t frameIndex);
    bool resolve(Handle h, uint32_t version,
                 VkBuffer& outBuffer, VkDeviceSize& outOffset) const;

    uint64_t usedBytes() const;

private:
    uint32_t bucketIndexFor(VkDeviceSize size) const;
    BufferBucket* createBucket(VkDeviceSize slotSize);
    bool allocateSlot(VkDeviceSize size, BufferAlloc& out);
    void freeSlot(const BufferAlloc& a);
    void evictIfNeeded();

    VulkanContext* ctx_;
    std::unique_ptr<IEvictionPolicy> policy_;
    uint64_t budgetBytes_;
    static constexpr VkDeviceSize kSlotSizes[] = {
        4 * 1024, 16 * 1024, 64 * 1024, 256 * 1024, 1024 * 1024 };
    static constexpr uint32_t kSlotsPerBucket = 64;

    std::vector<BufferBucket> buckets_;
    std::vector<VertexEntry> entries_; // index = handle.index
    std::vector<std::pair<Handle, uint32_t>> pendingUploads_;
    uint64_t frameIndex_ = 0;
    uint64_t usedBytes_ = 0;
};

} // namespace klib::krender

namespace klib::krender {

inline VertexBufferCache::VertexBufferCache(VulkanContext* ctx,
                                     std::unique_ptr<IEvictionPolicy> policy,
                                     uint64_t budgetBytes)
    : ctx_(ctx), policy_(std::move(policy)), budgetBytes_(budgetBytes) {}

inline VertexBufferCache::~VertexBufferCache() {
    for (auto& b : buckets_) {
        vkDestroyBuffer(ctx_->device, b.buffer, nullptr);
        vkFreeMemory(ctx_->device, b.memory, nullptr);
    }
}

inline uint32_t VertexBufferCache::bucketIndexFor(VkDeviceSize size) const {
    for (uint32_t i = 0; i < std::size(kSlotSizes); ++i)
        if (size <= kSlotSizes[i]) return i;
    return std::size(kSlotSizes); // 超大资源走单独分配（生产实现）
}

inline BufferBucket* VertexBufferCache::createBucket(VkDeviceSize slotSize) {
    BufferBucket b;
    b.slotSize = slotSize;
    b.slotCount = kSlotsPerBucket;
    VkDeviceSize total = slotSize * b.slotCount;

    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = total;
    ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(ctx_->device, &ci, nullptr, &b.buffer);

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(ctx_->device, b.buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = ctx_->findMemoryType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(ctx_->device, &ai, nullptr, &b.memory);
    vkBindBufferMemory(ctx_->device, b.buffer, b.memory, 0);

    b.freeSlots.resize(b.slotCount);
    for (uint32_t i = 0; i < b.slotCount; ++i) b.freeSlots[i] = i;

    buckets_.push_back(std::move(b));
    usedBytes_ += total;
    return &buckets_.back();
}

inline bool VertexBufferCache::allocateSlot(VkDeviceSize size, BufferAlloc& out) {
    uint32_t bi = bucketIndexFor(size);
    if (bi >= std::size(kSlotSizes)) return false;
    // 找有空槽的 bucket
    for (auto& b : buckets_) {
        if (b.slotSize == kSlotSizes[bi] && !b.freeSlots.empty()) {
            uint32_t slot = b.freeSlots.back();
            b.freeSlots.pop_back();
            out.bufferIndex = static_cast<uint32_t>(&b - buckets_.data());
            out.offset = static_cast<VkDeviceSize>(slot) * b.slotSize;
            out.size = b.slotSize;
            return true;
        }
    }
    if (usedBytes_ + kSlotSizes[bi] * kSlotsPerBucket > budgetBytes_)
        evictIfNeeded();
    if (usedBytes_ + kSlotSizes[bi] * kSlotsPerBucket > budgetBytes_)
        return false;
    BufferBucket* nb = createBucket(kSlotSizes[bi]);
    uint32_t slot = nb->freeSlots.back();
    nb->freeSlots.pop_back();
    out.bufferIndex = static_cast<uint32_t>(buckets_.size() - 1);
    out.offset = static_cast<VkDeviceSize>(slot) * nb->slotSize;
    out.size = nb->slotSize;
    return true;
}

inline void VertexBufferCache::freeSlot(const BufferAlloc& a) {
    if (a.bufferIndex >= buckets_.size()) return;
    auto& b = buckets_[a.bufferIndex];
    uint32_t slot = static_cast<uint32_t>(a.offset / b.slotSize);
    b.freeSlots.push_back(slot);
}

inline void VertexBufferCache::insertOrUpdate(Handle h, uint32_t version, const VertexData& data) {
    if (h.index >= entries_.size()) entries_.resize(h.index + 1);
    auto& e = entries_[h.index];
    if (e.handle == h) {
        if (e.version != version) {
            e.version = version;
            pendingUploads_.emplace_back(h, version);
        }
        return;
    }
    BufferAlloc alloc;
    if (!allocateSlot(data.size, alloc)) return;
    e = {};
    e.handle = h;
    e.version = version;
    e.alloc = alloc;
    e.alive = true;
    e.lastUsedFrame = frameIndex_;
    policy_->onInsert(h, alloc.size, frameIndex_);
    pendingUploads_.emplace_back(h, version);
}

inline void VertexBufferCache::prepare(
    const std::vector<std::pair<Handle, uint32_t>>& required, uint64_t frameIndex) {
    frameIndex_ = frameIndex;
    for (auto& e : entries_) e.pinned = false;
    for (auto& [h, ver] : required) {
        if (h.index >= entries_.size()) continue;
        auto& e = entries_[h.index];
        if (e.handle == h && e.alive) {
            e.pinned = true;
            if (e.version != ver) {
                e.version = ver;
                pendingUploads_.emplace_back(h, ver);
            }
        }
    }
}

inline void VertexBufferCache::flushUploads(VkCommandBuffer /*cmd*/) {
    // 生产：遍历 pendingUploads_，从 CPU 源拿 VertexData，调 ctx_->uploadToBuffer
    // for each h: ctx_->uploadToBuffer(bucket.buffer, e.alloc.offset, data.size, data.data)
    pendingUploads_.clear();
}

inline void VertexBufferCache::endFrame(uint64_t frameIndex) {
    frameIndex_ = frameIndex;
    for (auto& e : entries_) e.pinned = false;
    evictIfNeeded();
    policy_->onFrameEnd(frameIndex_);
}

inline bool VertexBufferCache::resolve(Handle h, uint32_t version,
                                VkBuffer& outBuffer, VkDeviceSize& outOffset) const {
    if (h.index >= entries_.size()) return false;
    const auto& e = entries_[h.index];
    if (e.handle != h || !e.alive || e.version != version) return false;
    if (e.alloc.bufferIndex >= buckets_.size()) return false;
    outBuffer = buckets_[e.alloc.bufferIndex].buffer;
    outOffset = e.alloc.offset;
    return true;
}

inline uint64_t VertexBufferCache::usedBytes() const { return usedBytes_; }

inline void VertexBufferCache::evictIfNeeded() {
    if (usedBytes_ <= budgetBytes_) return;
    // 简化：按 LRU 驱逐直到回预算
    std::vector<EvictionCandidate> cands;
    for (auto& e : entries_) {
        if (!e.alive) continue;
        cands.push_back({ e.handle, e.version, e.lastUsedFrame,
            e.alloc.size, e.accessCount, e.pinned });
    }
    auto victims = policy_->selectVictims(cands, usedBytes_ - budgetBytes_);
    for (size_t idx : victims) {
        Handle h = cands[idx].handle;
        if (h.index >= entries_.size()) continue;
        auto& e = entries_[h.index];
        if (!e.alive || e.pinned) continue;
        freeSlot(e.alloc);
        e.alive = false;
        e.handle = {};
    }
}

} // namespace klib::krender