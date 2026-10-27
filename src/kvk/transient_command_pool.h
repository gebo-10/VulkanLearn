#include <kvk/command_pool.h>
#include <kvk/fence.h>

namespace klib::vk {

// TransientCommandPool.h
class TransientCommandPool {
public:
    TransientCommandPool() = default;
    ~TransientCommandPool() { destroy(); }
    TransientCommandPool(const TransientCommandPool&) = delete;
    TransientCommandPool& operator=(const TransientCommandPool&) = delete;

    void initialize(DevicePtr device, uint32_t queueFamilyIndex) {
        if (!device) {
            throw std::invalid_argument(
                "TransientCommandPool requires a valid Device");
        }
        device_ = std::move(device);
        pool_ = std::make_shared<CommandPool>(
            device_, queueFamilyIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    }

    void destroy() {
        pool_.reset();
        device_.reset();
    }

    // 同步一次性执行：阻塞到 GPU 完成
    template <typename F>
    void oneShot(VkQueue queue, F&& record) {
        VkCommandBuffer cmd = allocate();
        begin(cmd);
        record(cmd);
        end(cmd);
        submit(queue, cmd);
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
        const auto commandBuffers =
            pool_->allocate(1, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        return commandBuffers.front();
    }

    void free(VkCommandBuffer cmd) {
        vkFreeCommandBuffers(device_->handle(), pool_->handle(), 1, &cmd);
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
        auto fence = std::make_shared<Fence>(device_);
        submitWithFence(queue, cmd, fence->handle());
        fence->wait();
    }

    static void submitWithFence(VkQueue queue, VkCommandBuffer cmd,
                                VkFence fence) {
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        info.commandBufferCount = 1;
        info.pCommandBuffers    = &cmd;
        checkVk(vkQueueSubmit(queue, 1, &info, fence), "vkQueueSubmit");
    }

    DevicePtr device_;
    CommandPoolPtr pool_;
};
}