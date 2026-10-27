#pragma once

#include <kvk/instance.h>

#include <memory>
#include <stdexcept>
#include <utility>

namespace klib::vk {

class Surface {
public:
    Surface(InstancePtr instance, VkSurfaceKHR surface)
        : instanceOwner_(std::move(instance)), surface_(surface) {
        if (!instanceOwner_) {
            throw std::invalid_argument("Surface requires a valid Instance");
        }
        if (surface_ == VK_NULL_HANDLE) {
            throw std::invalid_argument("Surface requires a valid VkSurfaceKHR");
        }
    }

    ~Surface() {
        if (surface_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instanceOwner_->handle(), surface_, nullptr);
        }
    }

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    VkSurfaceKHR handle() const { return surface_; }

private:
    InstancePtr instanceOwner_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
};

using SurfacePtr = std::shared_ptr<Surface>;

}  // namespace klib::vk
