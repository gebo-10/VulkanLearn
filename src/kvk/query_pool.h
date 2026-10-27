#pragma once

#include <kvk/device.h>

#include <memory>
#include <vector>

namespace klib::vk {

class QueryPool {
public:
    QueryPool(DevicePtr device, VkQueryType queryType, uint32_t queryCount,
              VkQueryPipelineStatisticFlags pipelineStatistics = 0)
        : QueryPool(std::move(device),
                    makeCreateInfo(queryType, queryCount,
                                   pipelineStatistics)) {}

    QueryPool(DevicePtr device, VkQueryPoolCreateInfo createInfo)
        : device_(std::move(device)), queryCount_(createInfo.queryCount) {
        if (!device_) {
            throw std::invalid_argument("QueryPool requires a valid Device");
        }
        createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        checkVk(vkCreateQueryPool(device_->handle(), &createInfo, nullptr,
                                  &queryPool_),
                "vkCreateQueryPool");
    }

    ~QueryPool() {
        if (queryPool_ != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device_->handle(), queryPool_, nullptr);
        }
    }

    QueryPool(const QueryPool&) = delete;
    QueryPool& operator=(const QueryPool&) = delete;
    QueryPool(QueryPool&&) = delete;
    QueryPool& operator=(QueryPool&&) = delete;

    [[nodiscard]] VkQueryPool handle() const noexcept { return queryPool_; }
    [[nodiscard]] uint32_t queryCount() const noexcept { return queryCount_; }
    [[nodiscard]] bool valid() const noexcept {
        return queryPool_ != VK_NULL_HANDLE;
    }

    void reset(uint32_t firstQuery, uint32_t queryCount) const {
        if (firstQuery > queryCount_ || queryCount > queryCount_ - firstQuery) {
            throw std::out_of_range("Query range exceeds QueryPool size");
        }
        vkResetQueryPool(device_->handle(), queryPool_, firstQuery, queryCount);
    }

    void reset() const { reset(0, queryCount_); }

    std::vector<uint64_t> results(uint32_t firstQuery, uint32_t count,
                                  VkQueryResultFlags flags) const {
        if (firstQuery > queryCount_ || count > queryCount_ - firstQuery) {
            throw std::out_of_range("Query range exceeds QueryPool size");
        }
        std::vector<uint64_t> values(count);
        checkVk(vkGetQueryPoolResults(
                    device_->handle(), queryPool_, firstQuery, count,
                    values.size() * sizeof(uint64_t), values.data(),
                    sizeof(uint64_t), flags | VK_QUERY_RESULT_64_BIT),
                "vkGetQueryPoolResults");
        return values;
    }

private:
    DevicePtr device_;
    VkQueryPool queryPool_ = VK_NULL_HANDLE;
    uint32_t queryCount_ = 0;

    static VkQueryPoolCreateInfo makeCreateInfo(
        VkQueryType queryType, uint32_t queryCount,
        VkQueryPipelineStatisticFlags pipelineStatistics) {
        VkQueryPoolCreateInfo createInfo{};
        createInfo.queryType = queryType;
        createInfo.queryCount = queryCount;
        createInfo.pipelineStatistics = pipelineStatistics;
        return createInfo;
    }
};

using QueryPoolPtr = std::shared_ptr<QueryPool>;

}  // namespace klib::vk
