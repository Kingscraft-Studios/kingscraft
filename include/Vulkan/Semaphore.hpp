#pragma once

#include "Device.hpp"

namespace kc {

    class Semaphore {
    public:
        explicit Semaphore(Device& device);
        ~Semaphore();

        Semaphore(const Semaphore&) = delete;
        Semaphore& operator=(const Semaphore&) = delete;

        Semaphore(Semaphore&& other) noexcept;
        Semaphore& operator=(Semaphore&& other) noexcept;

        VkSemaphore getHandle() const { return semaphore_; }
        void reset();

        // Detach the underlying handle so it can be destroyed later (e.g. deferred
        // destroy in a free-list). After release() the wrapper owns nothing.
        VkSemaphore release();

    private:
        Device& device_;
        VkSemaphore semaphore_ = VK_NULL_HANDLE;
    };

} // namespace kc