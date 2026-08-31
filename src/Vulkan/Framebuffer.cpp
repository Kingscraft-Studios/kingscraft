#include "Vulkan/Framebuffer.hpp"
#include <stdexcept>

namespace lve {

    Framebuffer::Framebuffer(Device& device, const VkFramebufferCreateInfo& info)
        : device_(device) {
        if (vkCreateFramebuffer(device_.device(), &info, nullptr, &framebuffer_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }

    Framebuffer::~Framebuffer() {
        if (framebuffer_ != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device_.device(), framebuffer_, nullptr);
    }

    Framebuffer::Framebuffer(Framebuffer&& other) noexcept
        : device_(other.device_), framebuffer_(other.framebuffer_) {
        other.framebuffer_ = VK_NULL_HANDLE;
    }

    Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
        if (this != &other) {
            if (framebuffer_ != VK_NULL_HANDLE)
                vkDestroyFramebuffer(device_.device(), framebuffer_, nullptr);
            framebuffer_ = other.framebuffer_;
            other.framebuffer_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    void Framebuffer::reset() {
        if (framebuffer_ != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device_.device(), framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }

} // namespace lve