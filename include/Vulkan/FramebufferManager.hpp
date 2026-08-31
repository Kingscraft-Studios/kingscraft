#pragma once

#include "Device.hpp"
#include <vector>

namespace kc {

    class FramebufferManager {
    public:
        explicit FramebufferManager(Device& device);
        ~FramebufferManager();

        FramebufferManager(const FramebufferManager&) = delete;
        FramebufferManager& operator=(const FramebufferManager&) = delete;

        void createFramebuffers(const std::vector<VkImageView>& imageViews, VkRenderPass renderPass, VkExtent2D extent);
        void destroyFramebuffers();

        VkFramebuffer getFramebuffer(int index) const { return framebuffers_[index]; }
        size_t size() const { return framebuffers_.size(); }

    private:
        Device& device_;
        std::vector<VkFramebuffer> framebuffers_;
    };

} // namespace kc
