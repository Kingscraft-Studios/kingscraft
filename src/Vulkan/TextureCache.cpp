#include "Vulkan/TextureCache.hpp"
#include "Core/Blocks/Block.hpp"
#include "Core/Registry.hpp"
#include "Core/Resources/BlockModel.hpp"
#include <stb_image.h>
#include <cstring>
#include <cmath>
#include <stdexcept>

namespace lve {

TextureCache::TextureCache(Device& device) : device_(device) {}

TextureCache::~TextureCache() {
    cleanup();
}

void TextureCache::cleanup() {
    if (descriptorSet_ != VK_NULL_HANDLE) {
        if (descriptorPool_ != VK_NULL_HANDLE && descriptorSetLayout_ != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(device_.device(), descriptorPool_, 1, &descriptorSet_);
        }
        descriptorSet_ = VK_NULL_HANDLE;
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_.device(), descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_.device(), descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
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
    layerCount_ = 0;
}

void TextureCache::updateFromRegistry() {
    cleanup();

    auto& registry = Registry<Block>::getRegistry();
    int blockCount = registry.size();

    struct DecodedTex {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
    };
    std::vector<DecodedTex> decoded;

    for (int i = 0; i < blockCount; ++i) {
        Block* block = registry.get(i);
        if (!block) continue;
        const auto& model = block->getModel();
        for (const auto& texData : model.getTextures()) {
            if (!texData.isValid()) continue;
            int w, h, channels;
            unsigned char* pixels = stbi_load_from_memory(
                reinterpret_cast<const stbi_uc*>(texData.rawData.data()),
                static_cast<int>(texData.rawData.size()),
                &w, &h, &channels, STBI_rgb_alpha);
            if (!pixels) continue;
            DecodedTex dec;
            dec.width = w;
            dec.height = h;
            dec.pixels.assign(pixels, pixels + w * h * 4);
            stbi_image_free(pixels);
            decoded.push_back(std::move(dec));
        }
    }

    if (decoded.empty() || decoded[0].width == 0 || decoded[0].height == 0)
        return;

    // Patch per-block texture base offsets so mesher can compute global texIndex
    {
        int globalIdx = 0;
        for (int i = 0; i < blockCount; ++i) {
            Block* b = registry.get(i);
            if (!b) continue;
            b->setTextureBaseOffset(globalIdx);
            globalIdx += static_cast<int>(b->getModel().getTextures().size());
        }
    }

    layerCount_ = static_cast<uint32_t>(decoded.size());
    int texW = decoded[0].width;
    int texH = decoded[0].height;
    int mipCount = static_cast<int>(std::floor(std::log2(std::max(texW, texH)))) + 1;
    VkDeviceSize layerSize = static_cast<VkDeviceSize>(texW) * texH * 4;
    VkDeviceSize totalSize = layerSize * layerCount_;

    // Staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    device_.createBuffer(
        totalSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingMemory);

    void* mapped;
    vkMapMemory(device_.device(), stagingMemory, 0, totalSize, 0, &mapped);
    for (uint32_t i = 0; i < layerCount_; ++i) {
        std::memcpy(static_cast<char*>(mapped) + i * layerSize,
                    decoded[i].pixels.data(), layerSize);
    }
    vkUnmapMemory(device_.device(), stagingMemory);

    // Create texture array image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(texW);
    imageInfo.extent.height = static_cast<uint32_t>(texH);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = static_cast<uint32_t>(mipCount);
    imageInfo.arrayLayers = layerCount_;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    device_.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                image_, imageMemory_);

    // Transition all mip levels to transfer dst
    device_.transitionImageLayout(image_, VK_FORMAT_R8G8B8A8_SRGB,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  layerCount_, static_cast<uint32_t>(mipCount));

    // Copy staging to image
    device_.copyBufferToImage(stagingBuffer, image_,
                              static_cast<uint32_t>(texW),
                              static_cast<uint32_t>(texH),
                              layerCount_);

    // Generate mip chain
    {
        VkCommandBuffer cmd = device_.beginSingleTimeCommands();

        for (int mip = 1; mip < mipCount; ++mip) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image_;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = static_cast<uint32_t>(mip - 1);
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = layerCount_;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);

            int srcW = std::max(1, texW >> (mip - 1));
            int srcH = std::max(1, texH >> (mip - 1));
            int dstW = std::max(1, texW >> mip);
            int dstH = std::max(1, texH >> mip);

            VkImageBlit blit{};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = static_cast<uint32_t>(mip - 1);
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = layerCount_;
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {srcW, srcH, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = static_cast<uint32_t>(mip);
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = layerCount_;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {dstW, dstH, 1};

            vkCmdBlitImage(cmd,
                image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit, VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        // Transition last mip to shader read
        VkImageMemoryBarrier lastBarrier{};
        lastBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        lastBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        lastBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        lastBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        lastBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        lastBarrier.image = image_;
        lastBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        lastBarrier.subresourceRange.baseMipLevel = static_cast<uint32_t>(mipCount - 1);
        lastBarrier.subresourceRange.levelCount = 1;
        lastBarrier.subresourceRange.baseArrayLayer = 0;
        lastBarrier.subresourceRange.layerCount = layerCount_;
        lastBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        lastBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &lastBarrier);

        device_.endSingleTimeCommands(cmd);
    }

    vkDestroyBuffer(device_.device(), stagingBuffer, nullptr);
    vkFreeMemory(device_.device(), stagingMemory, nullptr);

    // Image view (array)
    imageView_ = device_.createImageView(image_, VK_FORMAT_R8G8B8A8_SRGB,
                                         VK_IMAGE_ASPECT_COLOR_BIT,
                                         static_cast<uint32_t>(mipCount), 0,
                                         VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                                         layerCount_);

    // Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipCount);
    samplerInfo.mipLodBias = 0.0f;

    if (vkCreateSampler(device_.device(), &samplerInfo, nullptr, &sampler_) != VK_SUCCESS)
        throw std::runtime_error("failed to create texture cache sampler!");

    // Descriptor set layout
    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = 0;
    layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBinding.descriptorCount = 1;
    layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &layoutBinding;

    if (vkCreateDescriptorSetLayout(device_.device(), &layoutInfo, nullptr,
                                    &descriptorSetLayout_) != VK_SUCCESS)
        throw std::runtime_error("failed to create texture cache descriptor set layout!");

    // Descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr,
                               &descriptorPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create texture cache descriptor pool!");

    // Allocate set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout_;

    if (vkAllocateDescriptorSets(device_.device(), &allocInfo, &descriptorSet_) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate texture cache descriptor set!");

    // Write descriptor
    VkDescriptorImageInfo imageInfoDesc{};
    imageInfoDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfoDesc.imageView = imageView_;
    imageInfoDesc.sampler = sampler_;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet_;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfoDesc;

    vkUpdateDescriptorSets(device_.device(), 1, &descriptorWrite, 0, nullptr);
}

} // namespace lve
