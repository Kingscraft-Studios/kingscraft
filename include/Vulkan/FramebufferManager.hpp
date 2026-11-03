#pragma once

#include "Device.hpp"
#include "Framebuffer.hpp"
#include <memory>
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

        VkFramebuffer getFramebuffer(int index) const { return framebuffers_[index]->getHandle(); }
        size_t size() const { return framebuffers_.size(); }

    private:
        Device& device_;
        std::vector<std::unique_ptr<Framebuffer>> framebuffers_;
    };

} // namespace kc
