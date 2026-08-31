#pragma once

#include "Device.hpp"

namespace lve {

    class Framebuffer {
    public:
        Framebuffer(Device& device, const VkFramebufferCreateInfo& info);
        ~Framebuffer();

        Framebuffer(const Framebuffer&) = delete;
        Framebuffer& operator=(const Framebuffer&) = delete;

        Framebuffer(Framebuffer&& other) noexcept;
        Framebuffer& operator=(Framebuffer&& other) noexcept;

        VkFramebuffer getHandle() const { return framebuffer_; }
        void reset();

    private:
        Device& device_;
        VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    };

} // namespace lve