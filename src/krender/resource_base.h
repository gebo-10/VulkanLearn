// include/renderer/ResourceBase.h
#pragma once
#include "handle.h"
#include <cstdint>

namespace klib::krender {

enum class ZeroRefPolicy {
    Evictable,   // 归零后进 LRU 池，显存压力时驱逐（默认：纹理/Mesh）
    Immediate,   // 归零后下一次 collectGarbage 物理销毁（默认：Shader/Material）
    Persistent,  // 归零后永不自动销毁，只能显式 destroy（UI 常驻图、全局材质）
};

enum class ResidencyHint : uint8_t {
    DiskOnly,   // 只在磁盘，用时才加载（大图、冷资源）
    RAM,        // 希望 CPU 侧常驻一份（便于快速重传，不上 GPU）
    VRAM,       // 希望 GPU 侧常驻（CPU 副本可释放）
    RAM_VRAM,   // 两级都常驻（热资源，如 UI 常用图标、字体图集）
};

enum class ResourceKind {
    Unknown,
    Mesh,
    Material,
    Texture,
    Shader,
    RenderBuffer
};

class ResourceBase {
public:
    explicit ResourceBase(Handle h,
                          ZeroRefPolicy policy = ZeroRefPolicy::Evictable,
                          ResidencyHint hint = ResidencyHint::VRAM)
        : handle_(h), zeroRefPolicy_(policy), residencyHint_(hint) {}
    virtual ~ResourceBase() = default;

    ResourceBase(const ResourceBase&) = delete;
    ResourceBase& operator=(const ResourceBase&) = delete;

    Handle          handle()         const { return handle_; }
    uint32_t        version()        const { return version_; }
    ZeroRefPolicy   zeroRefPolicy()  const { return zeroRefPolicy_; }
    ResidencyHint   residencyHint()  const { return residencyHint_; }
    virtual uint64_t sizeBytes() const { return 0; }

protected:
    friend class ResourceManager;

    void bumpVersion() { ++version_; }
    void setZeroRefPolicy(ZeroRefPolicy p) { zeroRefPolicy_ = p; }
    void setResidencyHint(ResidencyHint h) { residencyHint_ = h; }

    Handle         handle_;
    uint32_t       version_ = 0;
    ZeroRefPolicy  zeroRefPolicy_ = ZeroRefPolicy::Evictable;
    ResidencyHint  residencyHint_ = ResidencyHint::VRAM;
};

template <typename T>
struct ResourceTraits {
    static constexpr ZeroRefPolicy zeroRef = ZeroRefPolicy::Evictable;
    static constexpr ResourceKind kind = ResourceKind::Unknown;
};

} // namespace klib::krender