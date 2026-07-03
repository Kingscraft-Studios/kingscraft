#include "Vulkan/OffscreenTarget.hpp"
#include <stdexcept>

namespace lve {

    OffscreenTarget::OffscreenTarget(Device& device, VkExtent2D extent, VkFormat format, VkRenderPass renderPass)
        : device_(device), extent_(extent), format_(format) {
        createResources(extent, format, renderPass);
    }

    OffscreenTarget::~OffscreenTarget() {
        destroyResources();
    }

    void OffscreenTarget::resize(VkExtent2D newExtent, VkRenderPass renderPass) {
        if (newExtent.width == extent_.width && newExtent.height == extent_.height) return;
        destroyResources();
        extent_ = newExtent;
        createResources(extent_, format_, renderPass);
    }

    void OffscreenTarget::createResources(VkExtent2D extent, VkFormat format, VkRenderPass renderPass) {
        device_.createImage(
            extent.width, extent.height,
            format,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            image_,
            imageMemory_);

        imageView_ = device_.createImageView(
            image_, format,
            VK_IMAGE_ASPECT_COLOR_BIT);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        if (vkCreateSampler(device_.device(), &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create offscreen sampler!");
        }

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &imageView_;
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device_.device(), &fbInfo, nullptr, &framebuffer_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create offscreen framebuffer!");
        }

        VkCommandBuffer cmd = device_.beginSingleTimeCommands();
        transitionToReadOnly(cmd);
        device_.endSingleTimeCommands(cmd);
    }

    void OffscreenTarget::destroyResources() {
        if (framebuffer_ != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_.device(), framebuffer_, nullptr);
            framebuffer_ = VK_NULL_HANDLE;
        }
        if (sampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_.device(), sampler_, nullptr);
            sampler_ = VK_NULL_HANDLE;
        }
        if (imageView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_.device(), imageView_, nullptr);
            imageView_ = VK_NULL_HANDLE;
        }
        if (image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_.device(), image_, nullptr);
            image_ = VK_NULL_HANDLE;
        }
        if (imageMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_.device(), imageMemory_, nullptr);
            imageMemory_ = VK_NULL_HANDLE;
        }
    }

    void OffscreenTarget::transitionToReadOnly(VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.image = image_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

} // namespace lve
