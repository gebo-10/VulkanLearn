#pragma once

#include <cstdint>
#include <functional>

namespace LearnVulkan {

struct FramebufferExtent {
    uint32_t width;
    uint32_t height;
};

class Window {
public:
    using ResizeCallback = std::function<void()>;

    virtual ~Window() = default;

    virtual FramebufferExtent framebufferExtent() const = 0;
    virtual void setResizeCallback(ResizeCallback callback) = 0;
    virtual void pollEvents() = 0;
    virtual void waitEvents() = 0;
    virtual bool shouldClose() const = 0;
};

}  // namespace LearnVulkan
