#include "Vulkan/QueryPool.hpp"
#include <stdexcept>

namespace lve {

    QueryPool::QueryPool(Device& device, const VkQueryPoolCreateInfo& info)
        : device_(device) {
        if (vkCreateQueryPool(device_.device(), &info, nullptr, &pool_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create query pool!");
        }
    }

    QueryPool::~QueryPool() {
        if (pool_ != VK_NULL_HANDLE)
            vkDestroyQueryPool(device_.device(), pool_, nullptr);
    }

    QueryPool::QueryPool(QueryPool&& other) noexcept
        : device_(other.device_), pool_(other.pool_) {
        other.pool_ = VK_NULL_HANDLE;
    }

    QueryPool& QueryPool::operator=(QueryPool&& other) noexcept {
        if (this != &other) {
            if (pool_ != VK_NULL_HANDLE)
                vkDestroyQueryPool(device_.device(), pool_, nullptr);
            pool_ = other.pool_;
            other.pool_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    void QueryPool::reset() {
        if (pool_ != VK_NULL_HANDLE)
            vkDestroyQueryPool(device_.device(), pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }

} // namespace lve