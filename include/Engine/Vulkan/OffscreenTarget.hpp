#pragma once

#include "Device.hpp"
#include "Framebuffer.hpp"
#include "Image.hpp"
#include "ImageView.hpp"
#include "Sampler.hpp"

#include <memory>

namespace kc {

    class OffscreenTarget {
    public:
        OffscreenTarget(Device& device, VkExtent2D extent, VkFormat format, VkRenderPass renderPass);
        ~OffscreenTarget();

        OffscreenTarget(const OffscreenTarget&) = delete;
        OffscreenTarget& operator=(const OffscreenTarget&) = delete;

        void resize(VkExtent2D newExtent, VkRenderPass renderPass);

        VkImageView getImageView() const { return imageView_->getHandle(); }
        VkSampler getSampler() const { return sampler_->getHandle(); }
        VkFramebuffer getFramebuffer() const { return framebuffer_->getHandle(); }
        VkImage getImage() const { return image_->getHandle(); }

    private:
        void createResources(VkExtent2D extent, VkFormat format, VkRenderPass renderPass);
        void destroyResources();
        void transitionToReadOnly(VkCommandBuffer cmd);

        Device& device_;
        VkExtent2D extent_{};
        VkFormat format_{};

        std::unique_ptr<Image> image_;
        std::unique_ptr<ImageView> imageView_;
        std::unique_ptr<Sampler> sampler_;
        std::unique_ptr<Framebuffer> framebuffer_;
    };

} // namespace kc
