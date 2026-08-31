#include "Vulkan/Semaphore.hpp"
#include <stdexcept>

namespace lve {

    Semaphore::Semaphore(Device& device)
        : device_(device) {
        VkSemaphoreCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(device_.device(), &info, nullptr, &semaphore_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create semaphore!");
        }
    }

    Semaphore::~Semaphore() {
        if (semaphore_ != VK_NULL_HANDLE)
            vkDestroySemaphore(device_.device(), semaphore_, nullptr);
    }

    Semaphore::Semaphore(Semaphore&& other) noexcept
        : device_(other.device_), semaphore_(other.semaphore_) {
        other.semaphore_ = VK_NULL_HANDLE;
    }

    Semaphore& Semaphore::operator=(Semaphore&& other) noexcept {
        if (this != &other) {
            if (semaphore_ != VK_NULL_HANDLE)
                vkDestroySemaphore(device_.device(), semaphore_, nullptr);
            semaphore_ = other.semaphore_;
            other.semaphore_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    VkSemaphore Semaphore::release() {
        VkSemaphore handle = semaphore_;
        semaphore_ = VK_NULL_HANDLE;
        return handle;
    }

    void Semaphore::reset() {
        if (semaphore_ != VK_NULL_HANDLE)
            vkDestroySemaphore(device_.device(), semaphore_, nullptr);
        semaphore_ = VK_NULL_HANDLE;
    }

} // namespace lve