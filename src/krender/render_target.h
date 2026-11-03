#pragma once

#include "render_texture.h"

#include <memory>
#include <stdexcept>
#include <utility>

namespace klib::krender {

class RenderTarget : public ResourceBase {
public:
    struct ScreenTargetTag {};
    static constexpr ScreenTargetTag Screen{};

    RenderTarget(Handle handle, ScreenTargetTag)
        : ResourceBase(handle), screen_(true) {}

    RenderTarget(Handle handle, RenderTexturePtr color,
                 RenderTexturePtr depth = {})
        : ResourceBase(handle),
          color_(std::move(color)),
          depth_(std::move(depth)) {
        if (!color_) {
            throw std::invalid_argument(
                "Offscreen RenderTarget requires a color RenderTexture");
        }
        if (depth_ &&
            (depth_->width() != color_->width() ||
             depth_->height() != color_->height())) {
            throw std::invalid_argument(
                "RenderTarget attachments must have identical dimensions");
        }
        if (depth_ && depth_->handle() == color_->handle()) {
            throw std::invalid_argument(
                "RenderTarget color and depth attachments must be distinct");
        }
    }

    bool isScreen() const noexcept { return screen_; }
    const RenderTexturePtr& color() const noexcept { return color_; }
    const RenderTexturePtr& depth() const noexcept { return depth_; }

    uint64_t sizeBytes() const noexcept override { return sizeof(*this); }

private:
    bool screen_ = false;
    RenderTexturePtr color_;
    RenderTexturePtr depth_;
};

using RenderTargetPtr = std::shared_ptr<RenderTarget>;

template <>
struct ResourceTraits<RenderTarget> {
    static constexpr ZeroRefPolicy zeroRef = ZeroRefPolicy::Immediate;
    static constexpr ResourceKind kind = ResourceKind::RenderTarget;
};

}  // namespace klib::krender
