#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace klib::krender {

// 稳定的资源身份：index + generation
struct Handle {
    uint32_t index      = UINT32_MAX;
    uint32_t generation = 0;
    bool valid() const { return index != UINT32_MAX; }
    bool operator==(const Handle& o) const {
        return index == o.index && generation == o.generation;
    }
    bool operator!=(const Handle& o) const { return !(*this == o); }
};

struct HandleHash {
    size_t operator()(const Handle& h) const {
        return (static_cast<size_t>(h.index) << 32) ^ h.generation;
    }
};

// 唯一权威：谁在什么时候递增 generation
class ResourceRegistry {
public:
    Handle allocate() {
        uint32_t idx;
        if (!freeSlots_.empty()) {
            idx = freeSlots_.back();
            freeSlots_.pop_back();
        } else {
            idx = static_cast<uint32_t>(generations_.size());
            generations_.push_back(0);
        }
        ++generations_[idx];          // ★ 只有这里改 generation
        return Handle{ idx, generations_[idx] };
    }

    void release(Handle h) {
        if (h.index >= generations_.size()) return;
        if (generations_[h.index] != h.generation) return; // 过期/重复释放
        freeSlots_.push_back(h.index);
    }

    bool valid(Handle h) const {
        return h.index < generations_.size()
            && generations_[h.index] == h.generation;
    }

private:
    std::vector<uint32_t> generations_;
    std::vector<uint32_t> freeSlots_;
};

} // namespace klib::krender