#pragma once

#include "resource_base.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

namespace klib::krender {

class ResourceManager {
public:
    ResourceManager() = default;
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    template <typename T, typename... Args>
    std::shared_ptr<T> create(Args&&... args) {
        return createResource<T>(ResourceTraits<T>::kind,
                                 ResourceTraits<T>::zeroRef,
                                 std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    std::shared_ptr<T> createAs(ResourceKind kind, Args&&... args) {
        return createResource<T>(kind, ResourceTraits<T>::zeroRef,
                                 std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    std::shared_ptr<T> createWithPolicy(ZeroRefPolicy policy,
                                        Args&&... args) {
        return createResource<T>(ResourceTraits<T>::kind, policy,
                                 std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    std::shared_ptr<T> createAs(ResourceKind kind, ZeroRefPolicy policy,
                                Args&&... args) {
        return createResource<T>(kind, policy,
                                 std::forward<Args>(args)...);
    }

    template <typename T>
    std::shared_ptr<T> get(Handle handle) {
        static_assert(std::is_base_of_v<ResourceBase, T>,
                      "Managed resources must derive from ResourceBase");
        if (!isValid(handle)) {
            return nullptr;
        }

        Slot& slot = slots_[handle.index];
        if (slot.type != std::type_index(typeid(T))) {
            return nullptr;
        }
        std::shared_ptr<ResourceBase> resource = slot.persistentResource;
        if (!resource) {
            resource = slot.resource.lock();
        }
        if (!resource || resource->handle() != handle) {
            return nullptr;
        }
        touchEvictionPool(handle);
        return std::static_pointer_cast<T>(std::move(resource));
    }

    bool isValid(Handle handle) const noexcept {
        return handle.valid() && handle.index < slots_.size() &&
               slots_[handle.index].active &&
               slots_[handle.index].generation == handle.generation;
    }

    std::optional<ResourceKind> kindOf(Handle handle) const noexcept {
        if (!isValid(handle)) {
            return std::nullopt;
        }
        return slots_[handle.index].kind;
    }

    template <typename T>
    std::shared_ptr<T> get(Handle handle, ResourceKind kind) {
        if (!isValid(handle) || slots_[handle.index].kind != kind) {
            return nullptr;
        }
        return get<T>(handle);
    }

    bool destroy(Handle handle) {
        if (!isValid(handle)) {
            return false;
        }
        removeFromEvictionPool(handle);
        releaseSlot(handle);
        --resourceCount_;
        return true;
    }

    template <typename T>
    bool destroy(Handle handle) {
        if (!isValid(handle) ||
            slots_[handle.index].type != std::type_index(typeid(T))) {
            return false;
        }
        return destroy(handle);
    }

    void collectGarbage() {
        for (size_t index = 0; index < slots_.size(); ++index) {
            Slot& slot = slots_[index];
            if (slot.active && slot.policy == ZeroRefPolicy::Immediate &&
                slot.resource.expired()) {
                releaseSlot({static_cast<uint32_t>(index), slot.generation});
                --resourceCount_;
            }
        }
    }

    uint64_t evictToFit(uint64_t targetBytes) {
        uint64_t freedBytes = 0;
        size_t candidatesRemaining = evictionPool_.size();
        while (freedBytes < targetBytes && candidatesRemaining > 0) {
            auto candidate = evictionPool_.begin();
            if (candidate->resource.use_count() > 1) {
                evictionPool_.splice(evictionPool_.end(), evictionPool_,
                                     candidate);
                --candidatesRemaining;
                continue;
            }

            const Handle handle = candidate->handle;
            freedBytes += candidate->resource->sizeBytes();
            evictionPool_.erase(candidate);
            if (isValid(handle)) {
                releaseSlot(handle);
                --resourceCount_;
            }
            candidatesRemaining = evictionPool_.size();
        }
        return freedBytes;
    }

    void clear() {
        evictionPool_.clear();
        freeSlots_.clear();
        for (size_t index = 0; index < slots_.size(); ++index) {
            Slot& slot = slots_[index];
            slot.resource.reset();
            slot.persistentResource.reset();
            slot.active = false;
            slot.type = std::type_index(typeid(void));
            slot.kind = ResourceKind::Unknown;
            slot.policy = ZeroRefPolicy::Immediate;
            freeSlots_.push_back(static_cast<uint32_t>(index));
        }
        resourceCount_ = 0;
    }

    size_t size() const noexcept { return resourceCount_; }
    bool empty() const noexcept { return resourceCount_ == 0; }
    size_t capacity() const noexcept { return slots_.size(); }

private:
    struct Slot {
        uint32_t generation = 0;
        std::type_index type{typeid(void)};
        ResourceKind kind = ResourceKind::Unknown;
        ZeroRefPolicy policy = ZeroRefPolicy::Immediate;
        std::weak_ptr<ResourceBase> resource;
        std::shared_ptr<ResourceBase> persistentResource;
        bool active = false;
    };

    struct EvictionEntry {
        Handle handle;
        std::shared_ptr<ResourceBase> resource;
    };

    std::vector<Slot> slots_;
    std::vector<uint32_t> freeSlots_;
    std::list<EvictionEntry> evictionPool_;
    size_t resourceCount_ = 0;

    template <typename T, typename... Args>
    std::shared_ptr<T> createResource(ResourceKind kind, ZeroRefPolicy policy,
                                      Args&&... args) {
        static_assert(std::is_base_of_v<ResourceBase, T>,
                      "Managed resources must derive from ResourceBase");
        if (policy != ZeroRefPolicy::Immediate &&
            policy != ZeroRefPolicy::Evictable &&
            policy != ZeroRefPolicy::Persistent) {
            throw std::invalid_argument("Invalid zero-reference resource policy");
        }

        const Handle handle = allocateHandle();
        std::shared_ptr<T> resource;
        try {
            resource =
                std::make_shared<T>(handle, std::forward<Args>(args)...);
        } catch (...) {
            releaseSlot(handle);
            throw;
        }

        Slot& slot = slots_[handle.index];
        resource->setZeroRefPolicy(policy);
        try {
            if (policy == ZeroRefPolicy::Evictable) {
                evictionPool_.push_back({handle, resource});
            } else if (policy == ZeroRefPolicy::Persistent) {
                slot.persistentResource = resource;
            }
        } catch (...) {
            releaseSlot(handle);
            throw;
        }

        slot.type = std::type_index(typeid(T));
        slot.kind = kind;
        slot.policy = policy;
        slot.resource = resource;
        slot.active = true;
        ++resourceCount_;
        return resource;
    }

    Handle allocateHandle() {
        uint32_t index;
        if (freeSlots_.empty()) {
            if (slots_.size() >= UINT32_MAX) {
                throw std::length_error(
                    "ResourceManager handle capacity exhausted");
            }
            index = static_cast<uint32_t>(slots_.size());
            slots_.emplace_back();
        } else {
            index = freeSlots_.back();
            freeSlots_.pop_back();
        }

        Slot& slot = slots_[index];
        if (++slot.generation == 0) {
            ++slot.generation;
        }
        slot.active = true;
        return {index, slot.generation};
    }

    void releaseSlot(Handle handle) {
        Slot& slot = slots_[handle.index];
        slot.resource.reset();
        slot.persistentResource.reset();
        slot.active = false;
        slot.type = std::type_index(typeid(void));
        slot.kind = ResourceKind::Unknown;
        slot.policy = ZeroRefPolicy::Immediate;
        freeSlots_.push_back(handle.index);
    }

    void touchEvictionPool(Handle handle) {
        for (auto entry = evictionPool_.begin(); entry != evictionPool_.end();
             ++entry) {
            if (entry->handle == handle) {
                evictionPool_.splice(evictionPool_.end(), evictionPool_, entry);
                return;
            }
        }
    }

    void removeFromEvictionPool(Handle handle) {
        for (auto entry = evictionPool_.begin(); entry != evictionPool_.end();) {
            if (entry->handle == handle) {
                entry = evictionPool_.erase(entry);
            } else {
                ++entry;
            }
        }
    }
};

}  // namespace klib::krender
