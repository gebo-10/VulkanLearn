// EvictionPolicy.h
#pragma once
#include "resource_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace klib::krender {

struct EvictionCandidate {
    Handle   handle;
    uint32_t version       = 0;
    uint64_t lastUsedFrame = 0;
    uint64_t sizeBytes     = 0;
    uint32_t accessCount   = 0;
    bool     pinned        = false;
};

class IEvictionPolicy {
public:
    virtual ~IEvictionPolicy() = default;
    virtual void onHit(Handle h, uint64_t frame) = 0;
    virtual void onInsert(Handle h, uint64_t sizeBytes, uint64_t frame) = 0;
    // 返回要驱逐的 candidates 下标；pinned 条目必须被跳过
    virtual std::vector<size_t> selectVictims(
        const std::vector<EvictionCandidate>& candidates,
        uint64_t overBudgetBytes) = 0;
    virtual void onFrameEnd(uint64_t) {}
};

// LRU：按 lastUsedFrame 升序，跳过 pinned
class LruEvictionPolicy : public IEvictionPolicy {
public:
    void onHit(Handle, uint64_t) override {}
    void onInsert(Handle, uint64_t, uint64_t) override {}
    std::vector<size_t> selectVictims(
        const std::vector<EvictionCandidate>& cands,
        uint64_t overBudget) override;
};

// FIFO：按插入顺序淘汰（适合每帧瞬态资源）
class FifoEvictionPolicy : public IEvictionPolicy {
public:
    void onHit(Handle, uint64_t) override {}
    void onInsert(Handle h, uint64_t, uint64_t) override {
        order_.push_back(h);
    }
    std::vector<size_t> selectVictims(
        const std::vector<EvictionCandidate>& cands,
        uint64_t overBudget) override;
    void onFrameEnd(uint64_t) override;
private:
    std::vector<Handle> order_;
};

} // namespace klib::krender

namespace klib::krender {

inline std::vector<size_t> LruEvictionPolicy::selectVictims(
    const std::vector<EvictionCandidate>& cands, uint64_t overBudget) {
    std::vector<size_t> order;
    order.reserve(cands.size());
    for (size_t i = 0; i < cands.size(); ++i)
        if (!cands[i].pinned) order.push_back(i);

    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return cands[a].lastUsedFrame < cands[b].lastUsedFrame;
    });

    std::vector<size_t> victims;
    uint64_t freed = 0;
    for (size_t i : order) {
        if (freed >= overBudget) break;
        victims.push_back(i);
        freed += cands[i].sizeBytes;
    }
    return victims;
}

inline std::vector<size_t> FifoEvictionPolicy::selectVictims(
    const std::vector<EvictionCandidate>& cands, uint64_t overBudget) {
    // 简化：退化为 LRU 语义，实践中需维护真正插入顺序表
    return LruEvictionPolicy{}.selectVictims(cands, overBudget);
}

inline void FifoEvictionPolicy::onFrameEnd(uint64_t) {}

} // namespace klib::krender