#include "Vulkan/DescriptorPool.hpp"
#include <stdexcept>

namespace lve {

    DescriptorPool::DescriptorPool(Device& device, const VkDescriptorPoolCreateInfo& info)
        : device_(device) {
        if (vkCreateDescriptorPool(device_.device(), &info, nullptr, &pool_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }

    DescriptorPool::DescriptorPool(Device& device)
        : device_(device) {}

    DescriptorPool::~DescriptorPool() {
        if (pool_ != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device_.device(), pool_, nullptr);
    }

    std::unique_ptr<DescriptorPool> DescriptorPool::adopt(Device& device, VkDescriptorPool pool) {
        std::unique_ptr<DescriptorPool> result(new DescriptorPool(device));
        result->pool_ = pool;
        return result;
    }

    DescriptorPool::DescriptorPool(DescriptorPool&& other) noexcept
        : device_(other.device_), pool_(other.pool_) {
        other.pool_ = VK_NULL_HANDLE;
    }

    DescriptorPool& DescriptorPool::operator=(DescriptorPool&& other) noexcept {
        if (this != &other) {
            if (pool_ != VK_NULL_HANDLE)
                vkDestroyDescriptorPool(device_.device(), pool_, nullptr);
            pool_ = other.pool_;
            other.pool_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    void DescriptorPool::reset() {
        if (pool_ != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device_.device(), pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }

} // namespace lve