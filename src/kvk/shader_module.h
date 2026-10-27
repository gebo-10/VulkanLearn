#pragma once

#include <kvk/device.h>

#include <memory>

namespace klib::vk {

class ShaderModule {
public:
    ShaderModule(DevicePtr device, VkShaderModuleCreateInfo createInfo) : device_(device) {
        if (!device_) {
            throw std::invalid_argument("ShaderModule requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        checkVk(vkCreateShaderModule(device_->handle(), &createInfo, nullptr,
                                     &module_),
                "vkCreateShaderModule");
    }

    ShaderModule(DevicePtr device, const std::vector<char>& code) : device_(device) {
        if (!device_) {
            throw std::invalid_argument("ShaderModule requires a valid Device");
        }
        if(code.empty()) {
            throw std::invalid_argument("ShaderModule requires non-empty code");
        }
        VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        checkVk(vkCreateShaderModule(device_->handle(), &createInfo, nullptr, &module_),
                "vkCreateShaderModule");
    }

    ~ShaderModule() {
        if (module_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_->handle(), module_, nullptr);
        }
    }

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;
    ShaderModule(ShaderModule&&) = delete;
    ShaderModule& operator=(ShaderModule&&) = delete;

    [[nodiscard]] VkShaderModule handle() const noexcept { return module_; }
    [[nodiscard]] bool valid() const noexcept {
        return module_ != VK_NULL_HANDLE;
    }

private:
    DevicePtr device_;
    VkShaderModule module_ = VK_NULL_HANDLE;
};

using ShaderModulePtr = std::shared_ptr<ShaderModule>;

}  // namespace klib::vk
