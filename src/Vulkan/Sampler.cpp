#include "Vulkan/Sampler.hpp"
#include <stdexcept>

namespace lve {

    Sampler::Sampler(Device& device, const VkSamplerCreateInfo& info)
        : device_(device) {
        if (vkCreateSampler(device_.device(), &info, nullptr, &sampler_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create sampler!");
        }
    }

    Sampler::~Sampler() {
        if (sampler_ != VK_NULL_HANDLE)
            vkDestroySampler(device_.device(), sampler_, nullptr);
    }

    Sampler::Sampler(Sampler&& other) noexcept
        : device_(other.device_), sampler_(other.sampler_) {
        other.sampler_ = VK_NULL_HANDLE;
    }

    Sampler& Sampler::operator=(Sampler&& other) noexcept {
        if (this != &other) {
            if (sampler_ != VK_NULL_HANDLE)
                vkDestroySampler(device_.device(), sampler_, nullptr);
            sampler_ = other.sampler_;
            other.sampler_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    void Sampler::reset() {
        if (sampler_ != VK_NULL_HANDLE)
            vkDestroySampler(device_.device(), sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }

} // namespace lve