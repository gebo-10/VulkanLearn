#pragma once
#include <krender/resource_base.h>
#include <utility>
#include <vector>

namespace klib::krender {

class Texture : public ResourceBase {
public:
    Texture(Handle h, uint32_t w, uint32_t height, std::vector<uint8_t> rgba)
        : ResourceBase(h), width_(w), height_(height), pixels_(std::move(rgba)) {}

    uint32_t width()  const { return width_; }
    uint32_t height() const { return height_; }
    const uint8_t* pixels() const { return pixels_.data(); }
    uint32_t bytesPerRow() const { return width_ * 4; }
    uint64_t sizeBytes() const noexcept override { return pixels_.size(); }

    // 尺寸不变的内容更新：只 bump version
    void updatePixels(std::vector<uint8_t> rgba) {
        pixels_ = std::move(rgba);
        bumpVersion();
    }

private:
    uint32_t width_, height_;
    std::vector<uint8_t> pixels_;
};

} // namespace rnd