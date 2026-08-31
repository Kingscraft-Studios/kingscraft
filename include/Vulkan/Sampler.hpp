#pragma once

#include "Device.hpp"

namespace lve {

    class Sampler {
    public:
        Sampler(Device& device, const VkSamplerCreateInfo& info);
        ~Sampler();

        Sampler(const Sampler&) = delete;
        Sampler& operator=(const Sampler&) = delete;

        Sampler(Sampler&& other) noexcept;
        Sampler& operator=(Sampler&& other) noexcept;

        VkSampler getHandle() const { return sampler_; }
        void reset();

    private:
        Device& device_;
        VkSampler sampler_ = VK_NULL_HANDLE;
    };

} // namespace lve