#pragma once

#include "Device.hpp"

namespace lve {

    class Fence {
    public:
        explicit Fence(Device& device, VkFenceCreateFlags flags = 0);
        ~Fence();

        Fence(const Fence&) = delete;
        Fence& operator=(const Fence&) = delete;

        Fence(Fence&& other) noexcept;
        Fence& operator=(Fence&& other) noexcept;

        VkFence getHandle() const { return fence_; }
        void reset();

        // Detach the underlying handle so it can be destroyed later (e.g. deferred
        // destroy in a free-list). After release() the wrapper owns nothing.
        VkFence release();

        VkResult wait(uint64_t timeout = UINT64_MAX) {
            return vkWaitForFences(device_.device(), 1, &fence_, VK_TRUE, timeout);
        }
        VkResult status() { return vkGetFenceStatus(device_.device(), fence_); }

    private:
        Device& device_;
        VkFence fence_ = VK_NULL_HANDLE;
    };

} // namespace lve