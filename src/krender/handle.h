#pragma once
#include <cstddef>
#include <cstdint>
namespace klib::krender {
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
} // namespace klib::krender