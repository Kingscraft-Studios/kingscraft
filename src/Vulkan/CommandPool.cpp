#include "Vulkan/CommandPool.hpp"
#include <stdexcept>

namespace lve {

    CommandPool::CommandPool(Device& device, uint32_t queueFamilyIndex,
                             VkCommandPoolCreateFlags flags)
        : device_(device) {
        VkCommandPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.flags = flags;
        info.queueFamilyIndex = queueFamilyIndex;
        if (vkCreateCommandPool(device_.device(), &info, nullptr, &pool_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create command pool!");
        }
    }

    CommandPool::~CommandPool() {
        if (pool_ != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_.device(), pool_, nullptr);
    }

    CommandPool::CommandPool(CommandPool&& other) noexcept
        : device_(other.device_), pool_(other.pool_) {
        other.pool_ = VK_NULL_HANDLE;
    }

    CommandPool& CommandPool::operator=(CommandPool&& other) noexcept {
        if (this != &other) {
            if (pool_ != VK_NULL_HANDLE)
                vkDestroyCommandPool(device_.device(), pool_, nullptr);
            pool_ = other.pool_;
            other.pool_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    std::vector<VkCommandBuffer> CommandPool::allocate(uint32_t count) const {
        std::vector<VkCommandBuffer> buffers(count);
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = pool_;
        allocInfo.commandBufferCount = count;
        if (vkAllocateCommandBuffers(device_.device(), &allocInfo, buffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers!");
        }
        return buffers;
    }

    void CommandPool::reset() {
        if (pool_ != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_.device(), pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }

} // namespace lve