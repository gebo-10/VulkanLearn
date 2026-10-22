#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace {

constexpr uint32_t kWindowWidth = 800;
constexpr uint32_t kWindowHeight = 600;
constexpr int kFramesInFlight = 2;

void checkVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(result) + ")");
    }
}

std::filesystem::path executableDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                            static_cast<DWORD>(path.size()));
    if (length == 0 || length == path.size()) {
        throw std::runtime_error("Failed to determine the executable directory");
    }
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

std::vector<char> readFile(const std::filesystem::path& path) {
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

struct QueueFamilies {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;

    bool complete() const { return graphics.has_value() && present.has_value(); }
};

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class TriangleDemo {
public:
    explicit TriangleDemo(GLFWwindow* window) : window_(window) {}

    ~TriangleDemo() { cleanup(); }

    void initialize() {
        createInstance();
        checkVk(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
                "glfwCreateWindowSurface");
        selectPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createImageViews();
        createGraphicsPipeline();
        createCommandPool();
        createCommandBuffers();
        createSyncObjects();
    }

    void drawFrame() {
        checkVk(vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE,
                                UINT64_MAX),
                "vkWaitForFences");

        uint32_t imageIndex = 0;
        const VkResult acquireResult = vkAcquireNextImageKHR(
            device_, swapchain_, UINT64_MAX, imageAvailable_[currentFrame_],
            VK_NULL_HANDLE, &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapchain();
            return;
        }
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
            checkVk(acquireResult, "vkAcquireNextImageKHR");
        }

        checkVk(vkResetFences(device_, 1, &inFlightFences_[currentFrame_]),
                "vkResetFences");
        checkVk(vkResetCommandBuffer(commandBuffers_[imageIndex], 0),
                "vkResetCommandBuffer");
        recordCommandBuffer(commandBuffers_[imageIndex], imageIndex);

        const VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable_[currentFrame_];
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinished_[currentFrame_];

        checkVk(vkQueueSubmit(graphicsQueue_, 1, &submitInfo,
                              inFlightFences_[currentFrame_]),
                "vkQueueSubmit");

        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished_[currentFrame_];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain_;
        presentInfo.pImageIndices = &imageIndex;

        const VkResult presentResult = vkQueuePresentKHR(presentQueue_, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
            presentResult == VK_SUBOPTIMAL_KHR || framebufferResized_) {
            framebufferResized_ = false;
            recreateSwapchain();
        } else {
            checkVk(presentResult, "vkQueuePresentKHR");
        }
        currentFrame_ = (currentFrame_ + 1) % kFramesInFlight;
    }

    static void framebufferSizeCallback(GLFWwindow* window, int, int) {
        auto* demo = static_cast<TriangleDemo*>(glfwGetWindowUserPointer(window));
        if (demo != nullptr) {
            demo->framebufferResized_ = true;
        }
    }

private:
    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    VkFormat swapchainImageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    std::array<VkSemaphore, kFramesInFlight> imageAvailable_{};
    std::array<VkSemaphore, kFramesInFlight> renderFinished_{};
    std::array<VkFence, kFramesInFlight> inFlightFences_{};
    size_t currentFrame_ = 0;
    bool framebufferResized_ = false;

    void createInstance() {
        VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        appInfo.pApplicationName = "Vulkan 1.4 Triangle";
        appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        appInfo.pEngineName = "VulkanLearn";
        appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_4;

        uint32_t extensionCount = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        if (extensions == nullptr || extensionCount == 0) {
            throw std::runtime_error("GLFW could not provide Vulkan instance extensions");
        }

        VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = extensionCount;
        createInfo.ppEnabledExtensionNames = extensions;
        checkVk(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");
    }

    QueueFamilies findQueueFamilies(VkPhysicalDevice device) const {
        QueueFamilies families;
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> properties(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());

        for (uint32_t i = 0; i < count; ++i) {
            if ((properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                families.graphics = i;
            }
            VkBool32 supportsPresent = VK_FALSE;
            checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_,
                                                         &supportsPresent),
                    "vkGetPhysicalDeviceSurfaceSupportKHR");
            if (supportsPresent == VK_TRUE) {
                families.present = i;
            }
            if (families.complete()) {
                break;
            }
        }
        return families;
    }

    SwapchainSupport querySwapchainSupport(VkPhysicalDevice device) const {
        SwapchainSupport support;
        checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                    device, surface_, &support.capabilities),
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        uint32_t count = 0;
        checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &count, nullptr),
                "vkGetPhysicalDeviceSurfaceFormatsKHR");
        support.formats.resize(count);
        if (count != 0) {
            checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                        device, surface_, &count, support.formats.data()),
                    "vkGetPhysicalDeviceSurfaceFormatsKHR");
        }

        count = 0;
        checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &count,
                                                          nullptr),
                "vkGetPhysicalDeviceSurfacePresentModesKHR");
        support.presentModes.resize(count);
        if (count != 0) {
            checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(
                        device, surface_, &count, support.presentModes.data()),
                    "vkGetPhysicalDeviceSurfacePresentModesKHR");
        }
        return support;
    }

    bool supportsRequiredExtensions(VkPhysicalDevice device) const {
        uint32_t count = 0;
        checkVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
                "vkEnumerateDeviceExtensionProperties");
        std::vector<VkExtensionProperties> extensions(count);
        checkVk(vkEnumerateDeviceExtensionProperties(device, nullptr, &count,
                                                      extensions.data()),
                "vkEnumerateDeviceExtensionProperties");
        return std::any_of(extensions.begin(), extensions.end(),
                           [](const VkExtensionProperties& extension) {
                               return std::string(extension.extensionName) ==
                                      VK_KHR_SWAPCHAIN_EXTENSION_NAME;
                           });
    }

    void selectPhysicalDevice() {
        uint32_t count = 0;
        checkVk(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
                "vkEnumeratePhysicalDevices");
        if (count == 0) {
            throw std::runtime_error("No Vulkan-capable GPU was found");
        }

        std::vector<VkPhysicalDevice> devices(count);
        checkVk(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
                "vkEnumeratePhysicalDevices");

        for (VkPhysicalDevice candidate : devices) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_4) {
                continue;
            }
            const QueueFamilies families = findQueueFamilies(candidate);
            const SwapchainSupport support = querySwapchainSupport(candidate);
            VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
            VkPhysicalDeviceFeatures2 features{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &dynamicRendering;
            vkGetPhysicalDeviceFeatures2(candidate, &features);
            if (families.complete() && supportsRequiredExtensions(candidate) &&
                !support.formats.empty() && !support.presentModes.empty() &&
                dynamicRendering.dynamicRendering == VK_TRUE) {
                physicalDevice_ = candidate;
                spdlog::info("Using Vulkan 1.4 device: {}", properties.deviceName);
                return;
            }
        }
        throw std::runtime_error(
            "No GPU supports Vulkan 1.4, dynamic rendering, and the required "
            "presentation capabilities");
    }

    void createLogicalDevice() {
        const QueueFamilies families = findQueueFamilies(physicalDevice_);
        const std::set<uint32_t> uniqueFamilies = {*families.graphics, *families.present};
        const float priority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        for (uint32_t family : uniqueFamilies) {
            VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &priority;
            queueInfos.push_back(queueInfo);
        }

        VkPhysicalDeviceFeatures features{};
        VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
        dynamicRendering.dynamicRendering = VK_TRUE;
        const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        createInfo.pNext = &dynamicRendering;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
        createInfo.pQueueCreateInfos = queueInfos.data();
        createInfo.pEnabledFeatures = &features;
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = extensions;
        checkVk(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
                "vkCreateDevice");
        vkGetDeviceQueue(device_, *families.graphics, 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, *families.present, 0, &presentQueue_);
    }

    VkSurfaceFormatKHR chooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& formats) const {
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return format;
            }
        }
        return formats.front();
    }

    VkPresentModeKHR choosePresentMode(
        const std::vector<VkPresentModeKHR>& modes) const {
        for (VkPresentModeKHR mode : modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const {
        if (capabilities.currentExtent.width !=
            std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        VkExtent2D extent{static_cast<uint32_t>(width),
                          static_cast<uint32_t>(height)};
        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
        return extent;
    }

    void createSwapchain() {
        const SwapchainSupport support = querySwapchainSupport(physicalDevice_);
        const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
        const VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
        swapchainExtent_ = chooseExtent(support.capabilities);

        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 &&
            imageCount > support.capabilities.maxImageCount) {
            imageCount = support.capabilities.maxImageCount;
        }

        const QueueFamilies families = findQueueFamilies(physicalDevice_);
        const uint32_t queueFamilyIndices[] = {*families.graphics, *families.present};
        VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        createInfo.surface = surface_;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = swapchainExtent_;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (families.graphics != families.present) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        createInfo.preTransform = support.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        checkVk(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_),
                "vkCreateSwapchainKHR");
        checkVk(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr),
                "vkGetSwapchainImagesKHR");
        swapchainImages_.resize(imageCount);
        checkVk(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount,
                                        swapchainImages_.data()),
                "vkGetSwapchainImagesKHR");
        swapchainImageFormat_ = surfaceFormat.format;
    }

    void createImageViews() {
        swapchainImageViews_.resize(swapchainImages_.size());
        for (size_t i = 0; i < swapchainImages_.size(); ++i) {
            VkImageViewCreateInfo createInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            createInfo.image = swapchainImages_[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = swapchainImageFormat_;
            createInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                                     VK_COMPONENT_SWIZZLE_IDENTITY,
                                     VK_COMPONENT_SWIZZLE_IDENTITY,
                                     VK_COMPONENT_SWIZZLE_IDENTITY};
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;
            checkVk(vkCreateImageView(device_, &createInfo, nullptr,
                                      &swapchainImageViews_[i]),
                    "vkCreateImageView");
        }
    }

    VkShaderModule createShaderModule(const std::vector<char>& code) const {
        VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module = VK_NULL_HANDLE;
        checkVk(vkCreateShaderModule(device_, &createInfo, nullptr, &module),
                "vkCreateShaderModule");
        return module;
    }

    void createGraphicsPipeline() {
        const auto shaderDirectory = executableDirectory() / L"shaders";
        const auto vertexCode = readFile(shaderDirectory / L"triangle.vert.spv");
        const auto fragmentCode = readFile(shaderDirectory / L"triangle.frag.spv");
        const VkShaderModule vertexShader = createShaderModule(vertexCode);
        VkShaderModule fragmentShader = VK_NULL_HANDLE;
        try {
            fragmentShader = createShaderModule(fragmentCode);
        } catch (...) {
            vkDestroyShaderModule(device_, vertexShader, nullptr);
            throw;
        }

        VkPipelineShaderStageCreateInfo vertexStage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertexStage.module = vertexShader;
        vertexStage.pName = "main";
        VkPipelineShaderStageCreateInfo fragmentStage{
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragmentStage.module = fragmentShader;
        fragmentStage.pName = "main";
        const VkPipelineShaderStageCreateInfo stages[] = {vertexStage, fragmentStage};

        VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport viewport{};
        viewport.width = static_cast<float>(swapchainExtent_.width);
        viewport.height = static_cast<float>(swapchainExtent_.height);
        viewport.maxDepth = 1.0f;
        VkRect2D scissor{{0, 0}, swapchainExtent_};
        VkPipelineViewportStateCreateInfo viewportState{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;

        VkPipelineMultisampleStateCreateInfo multisampling{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlending{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPipelineLayoutCreateInfo layoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        VkResult result = vkCreatePipelineLayout(device_, &layoutInfo, nullptr,
                                                 &pipelineLayout_);
        if (result == VK_SUCCESS) {
            VkPipelineRenderingCreateInfo renderingInfo{
                VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachmentFormats = &swapchainImageFormat_;

            VkGraphicsPipelineCreateInfo pipelineInfo{
                VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            pipelineInfo.pNext = &renderingInfo;
            pipelineInfo.stageCount = 2;
            pipelineInfo.pStages = stages;
            pipelineInfo.pVertexInputState = &vertexInput;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisampling;
            pipelineInfo.pColorBlendState = &colorBlending;
            pipelineInfo.layout = pipelineLayout_;
            pipelineInfo.renderPass = VK_NULL_HANDLE;
            result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1,
                                               &pipelineInfo, nullptr,
                                               &graphicsPipeline_);
        }
        vkDestroyShaderModule(device_, fragmentShader, nullptr);
        vkDestroyShaderModule(device_, vertexShader, nullptr);
        checkVk(result, "creating graphics pipeline");
    }

    void createCommandPool() {
        const QueueFamilies families = findQueueFamilies(physicalDevice_);
        VkCommandPoolCreateInfo createInfo{
            VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        createInfo.queueFamilyIndex = *families.graphics;
        checkVk(vkCreateCommandPool(device_, &createInfo, nullptr, &commandPool_),
                "vkCreateCommandPool");
    }

    void createCommandBuffers() {
        commandBuffers_.resize(swapchainImageViews_.size());
        VkCommandBufferAllocateInfo allocateInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocateInfo.commandPool = commandPool_;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount =
            static_cast<uint32_t>(commandBuffers_.size());
        checkVk(vkAllocateCommandBuffers(device_, &allocateInfo,
                                         commandBuffers_.data()),
                "vkAllocateCommandBuffers");
    }

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
        VkCommandBufferBeginInfo beginInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        checkVk(vkBeginCommandBuffer(commandBuffer, &beginInfo),
                "vkBeginCommandBuffer");

        VkImageMemoryBarrier toColorAttachment{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toColorAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toColorAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toColorAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toColorAttachment.image = swapchainImages_[imageIndex];
        toColorAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toColorAttachment.subresourceRange.levelCount = 1;
        toColorAttachment.subresourceRange.layerCount = 1;
        toColorAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &toColorAttachment);

        VkRenderingAttachmentInfo colorAttachment{
            VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        colorAttachment.imageView = swapchainImageViews_[imageIndex];
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue.color = {{0.04f, 0.05f, 0.08f, 1.0f}};

        VkRenderingInfo renderInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
        renderInfo.renderArea = {{0, 0}, swapchainExtent_};
        renderInfo.layerCount = 1;
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;
        vkCmdBeginRendering(commandBuffer, &renderInfo);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          graphicsPipeline_);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRendering(commandBuffer);

        VkImageMemoryBarrier toPresent{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toPresent.image = swapchainImages_[imageIndex];
        toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toPresent.subresourceRange.levelCount = 1;
        toPresent.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &toPresent);
        checkVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
    }

    void createSyncObjects() {
        VkSemaphoreCreateInfo semaphoreInfo{
            VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < kFramesInFlight; ++i) {
            checkVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                                      &imageAvailable_[i]),
                    "vkCreateSemaphore");
            checkVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                                      &renderFinished_[i]),
                    "vkCreateSemaphore");
            checkVk(vkCreateFence(device_, &fenceInfo, nullptr,
                                  &inFlightFences_[i]),
                    "vkCreateFence");
        }
    }

    void recreateSwapchain() {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        while (width == 0 || height == 0) {
            glfwWaitEvents();
            glfwGetFramebufferSize(window_, &width, &height);
        }

        checkVk(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
        cleanupSwapchain();
        createSwapchain();
        createImageViews();
        createGraphicsPipeline();
        createCommandBuffers();
    }

    void cleanupSwapchain() {
        if (commandPool_ != VK_NULL_HANDLE && !commandBuffers_.empty()) {
            vkFreeCommandBuffers(device_, commandPool_,
                                 static_cast<uint32_t>(commandBuffers_.size()),
                                 commandBuffers_.data());
            commandBuffers_.clear();
        }
        if (graphicsPipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
            graphicsPipeline_ = VK_NULL_HANDLE;
        }
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
        for (VkImageView imageView : swapchainImageViews_) {
            vkDestroyImageView(device_, imageView, nullptr);
        }
        swapchainImageViews_.clear();
        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
        swapchainImages_.clear();
    }

    void cleanup() {
        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
            cleanupSwapchain();
            for (int i = 0; i < kFramesInFlight; ++i) {
                if (imageAvailable_[i] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device_, imageAvailable_[i], nullptr);
                }
                if (renderFinished_[i] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device_, renderFinished_[i], nullptr);
                }
                if (inFlightFences_[i] != VK_NULL_HANDLE) {
                    vkDestroyFence(device_, inFlightFences_[i], nullptr);
                }
            }
            if (commandPool_ != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_, commandPool_, nullptr);
            }
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
        if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }
};

}  // namespace

int main() {
    spdlog::info("Starting Vulkan 1.4 triangle demo");
    if (glfwInit() != GLFW_TRUE) {
        spdlog::error("Failed to initialize GLFW");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight,
                                          "Vulkan 1.4 - Triangle", nullptr, nullptr);
    if (window == nullptr) {
        spdlog::error("Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }

    int exitCode = 0;
    try {
        TriangleDemo demo(window);
        glfwSetWindowUserPointer(window, &demo);
        glfwSetFramebufferSizeCallback(window, TriangleDemo::framebufferSizeCallback);
        demo.initialize();

        while (glfwWindowShouldClose(window) == GLFW_FALSE) {
            glfwPollEvents();
            demo.drawFrame();
        }
    } catch (const std::exception& error) {
        spdlog::error("Vulkan demo failed: {}", error.what());
        exitCode = 1;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
