#pragma once

#include "Device.hpp"

namespace lve {

    class OffscreenTarget {
    public:
        OffscreenTarget(Device& device, VkExtent2D extent, VkFormat format, VkRenderPass renderPass);
        ~OffscreenTarget();

        OffscreenTarget(const OffscreenTarget&) = delete;
        OffscreenTarget& operator=(const OffscreenTarget&) = delete;

        void resize(VkExtent2D newExtent, VkRenderPass renderPass);

        VkImageView getImageView() const { return imageView_; }
        VkSampler getSampler() const { return sampler_; }
        VkFramebuffer getFramebuffer() const { return framebuffer_; }
        VkImage getImage() const { return image_; }

    private:
        void createResources(VkExtent2D extent, VkFormat format, VkRenderPass renderPass);
        void destroyResources();
        void transitionToReadOnly(VkCommandBuffer cmd);

        Device& device_;
        VkExtent2D extent_{};
        VkFormat format_{};

        VkImage image_ = VK_NULL_HANDLE;
        VkDeviceMemory imageMemory_ = VK_NULL_HANDLE;
        VkImageView imageView_ = VK_NULL_HANDLE;
        VkSampler sampler_ = VK_NULL_HANDLE;
        VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    };

} // namespace lve
