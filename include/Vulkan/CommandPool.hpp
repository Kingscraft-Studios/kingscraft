#pragma once

#include "Device.hpp"
#include <vector>

namespace lve {

    class CommandPool {
    public:
        CommandPool(Device& device, uint32_t queueFamilyIndex,
                    VkCommandPoolCreateFlags flags = 0);
        ~CommandPool();

        CommandPool(const CommandPool&) = delete;
        CommandPool& operator=(const CommandPool&) = delete;

        CommandPool(CommandPool&& other) noexcept;
        CommandPool& operator=(CommandPool&& other) noexcept;

        VkCommandPool getHandle() const { return pool_; }
        std::vector<VkCommandBuffer> allocate(uint32_t count) const;
        void reset();

    private:
        Device& device_;
        VkCommandPool pool_ = VK_NULL_HANDLE;
    };

} // namespace lve