#include <kvk/utils.h>
namespace klib::vk {

// TransientCommandPool.h
class TransientCommandPool {
public:
    TransientCommandPool() = default;
    ~TransientCommandPool() { destroy(); }
    TransientCommandPool(const TransientCommandPool&) = delete;
    TransientCommandPool& operator=(const TransientCommandPool&) = delete;

    void initialize(VkDevice device, uint32_t queueFamilyIndex) {
        device_ = device;
        VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        info.queueFamilyIndex = queueFamilyIndex;
        checkVk(vkCreateCommandPool(device_, &info, nullptr, &pool_),
                "vkCreateCommandPool(transient)");
    }

    void destroy() {
        if (pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, pool_, nullptr);
            pool_ = VK_NULL_HANDLE;
        }
    }

    // 同步一次性执行：阻塞到 GPU 完成
    template <typename F>
    void oneShot(VkQueue queue, F&& record) {
        VkCommandBuffer cmd = allocate();
        begin(cmd);
        record(cmd);
        end(cmd);
        submit(queue, cmd);           // 内部建 fence + 等待
        free(cmd);
    }

    // 异步一次性执行：返回后调用方负责 fence 等待
    template <typename F>
    void oneShotAsync(VkQueue queue, VkFence fence, F&& record) {
        VkCommandBuffer cmd = allocate();
        begin(cmd);
        record(cmd);
        end(cmd);
        submitWithFence(queue, cmd, fence);
        free(cmd);                     // 提交后即可释放，driver 已复制
    }

    // 批量模式：一次录制多条命令、一次提交、一次等待
    template <typename F>
    void oneShotBatch(VkQueue queue, F&& recordAll) {
        VkCommandBuffer cmd = allocate();
        begin(cmd);
        recordAll(cmd);                // 回调里可以录任意多条命令
        end(cmd);
        submit(queue, cmd);
        free(cmd);
    }

private:
    VkCommandBuffer allocate() {
        VkCommandBufferAllocateInfo info{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        info.commandPool        = pool_;
        info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        checkVk(vkAllocateCommandBuffers(device_, &info, &cmd),
                "vkAllocateCommandBuffers");
        return cmd;
    }

    void free(VkCommandBuffer cmd) {
        vkFreeCommandBuffers(device_, pool_, 1, &cmd);
    }

    static void begin(VkCommandBuffer cmd) {
        VkCommandBufferBeginInfo info{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checkVk(vkBeginCommandBuffer(cmd, &info), "vkBeginCommandBuffer");
    }

    static void end(VkCommandBuffer cmd) {
        checkVk(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
    }

    // 同步提交：临时 fence + 等待，避免 vkQueueWaitIdle 阻塞整个队列
    void submit(VkQueue queue, VkCommandBuffer cmd) {
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        checkVk(vkCreateFence(device_, &fenceInfo, nullptr, &fence),
                "vkCreateFence");
        submitWithFence(queue, cmd, fence);
        checkVk(vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX),
                "vkWaitForFences");
        vkDestroyFence(device_, fence, nullptr);
    }

    static void submitWithFence(VkQueue queue, VkCommandBuffer cmd,
                                VkFence fence) {
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        info.commandBufferCount = 1;
        info.pCommandBuffers    = &cmd;
        checkVk(vkQueueSubmit(queue, 1, &info, fence), "vkQueueSubmit");
    }

    VkDevice      device_ = VK_NULL_HANDLE;
    VkCommandPool pool_   = VK_NULL_HANDLE;
};
}