#include "Vulkan/Fence.hpp"
#include <stdexcept>

namespace lve {

    Fence::Fence(Device& device, VkFenceCreateFlags flags)
        : device_(device) {
        VkFenceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        info.flags = flags;
        if (vkCreateFence(device_.device(), &info, nullptr, &fence_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create fence!");
        }
    }

    Fence::~Fence() {
        if (fence_ != VK_NULL_HANDLE)
            vkDestroyFence(device_.device(), fence_, nullptr);
    }

    Fence::Fence(Fence&& other) noexcept
        : device_(other.device_), fence_(other.fence_) {
        other.fence_ = VK_NULL_HANDLE;
    }

    Fence& Fence::operator=(Fence&& other) noexcept {
        if (this != &other) {
            if (fence_ != VK_NULL_HANDLE)
                vkDestroyFence(device_.device(), fence_, nullptr);
            fence_ = other.fence_;
            other.fence_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    VkFence Fence::release() {
        VkFence handle = fence_;
        fence_ = VK_NULL_HANDLE;
        return handle;
    }

    void Fence::reset() {
        if (fence_ != VK_NULL_HANDLE)
            vkDestroyFence(device_.device(), fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }

} // namespace lve