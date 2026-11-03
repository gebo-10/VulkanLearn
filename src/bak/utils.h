#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <vulkan/vulkan.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace klib::vk {
inline std::filesystem::path executableDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                            static_cast<DWORD>(path.size()));
    if (length == 0 || length == path.size()) {
        throw std::runtime_error("Failed to determine the executable directory");
    }
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

inline std::vector<char> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open shader: " + path.string());
    }

    const auto fileSize = file.tellg();
    if (fileSize <= 0 || fileSize % 4 != 0) {
        throw std::runtime_error("Invalid SPIR-V shader size: " + path.string());
    }

    std::vector<char> buffer(static_cast<size_t>(fileSize));
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    if (!file) {
        throw std::runtime_error("Failed to read shader: " + path.string());
    }
    return buffer;
}

}  // namespace klib::vk
