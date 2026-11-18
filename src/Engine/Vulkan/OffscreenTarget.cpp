#include "Vulkan/OffscreenTarget.hpp"

namespace kc {

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
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {extent.width, extent.height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        image_ = std::make_unique<Image>(device_, imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        imageView_ = std::make_unique<ImageView>(
            device_, image_->getHandle(), format, VK_IMAGE_ASPECT_COLOR_BIT);

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

        sampler_ = std::make_unique<Sampler>(device_, samplerInfo);

        VkImageView attachment = imageView_->getHandle();
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &attachment;
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1;

        framebuffer_ = std::make_unique<Framebuffer>(device_, fbInfo);

        VkCommandBuffer cmd = device_.beginSingleTimeCommands();
        transitionToReadOnly(cmd);
        device_.endSingleTimeCommands(cmd);
    }

    void OffscreenTarget::destroyResources() {
        // Reverse creation order: framebuffer, sampler, view, image.
        framebuffer_.reset();
        sampler_.reset();
        imageView_.reset();
        image_.reset();
    }

    void OffscreenTarget::transitionToReadOnly(VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.image = image_->getHandle();
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

} // namespace kc
