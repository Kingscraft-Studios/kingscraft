#include "Vulkan/Texture.hpp"

#define STB_IMAGE_IMPLEMENTATION

#include <stb_image.h>
#include <stdexcept>

namespace kc {

    Texture::Texture(Device &device, const std::vector<char>& fileData) : device(device) {
        createTextureImage(fileData);
        createTextureImageView();
        createTextureSampler();
    }

    Texture::~Texture() {
        vkDestroySampler(device.device(), textureSampler, nullptr);
        vkDestroyImageView(device.device(), textureImageView, nullptr);
        vkDestroyImage(device.device(), textureImage, nullptr);
        vkFreeMemory(device.device(), textureImageMemory, nullptr);
    }

    void Texture::createTextureImage(const std::vector<char>& fileData) {
        int texWidth, texHeight, texChannels;
//        stbi_set_flip_vertically_on_load(true); only on 3d Model Rendering
        stbi_uc *pixels = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(fileData.data()),
            fileData.size(),
            &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels) {
            throw std::runtime_error("failed to load texture image!");
        }

        VkDeviceSize imageSize = texWidth * texHeight * 4;

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;

        // Create staging buffer with CPU visible memory
        device.createBuffer(
                imageSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffer,
                stagingBufferMemory);

        // Copy pixel data to staging buffer memory
        void *data;
        vkMapMemory(device.device(), stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, pixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(device.device(), stagingBufferMemory);

        stbi_image_free(pixels);

        // Create GPU local image with transfer destination & sampled usage
        device.createImage(
                texWidth,
                texHeight,
                VK_FORMAT_R8G8B8A8_SRGB,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                textureImage,
                textureImageMemory);

        // Transition image layout to receive data
        device.transitionImageLayout(textureImage,
                                        VK_FORMAT_R8G8B8A8_SRGB,
                                        VK_IMAGE_LAYOUT_UNDEFINED,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Copy buffer (CPU side) to image (GPU side)
        device.copyBufferToImage(stagingBuffer, textureImage,
                                    static_cast<uint32_t>(texWidth),
                                    static_cast<uint32_t>(texHeight), 1);

        // Transition image layout for shader read access
        device.transitionImageLayout(textureImage,
                                        VK_FORMAT_R8G8B8A8_SRGB,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkDestroyBuffer(device.device(), stagingBuffer, nullptr);
        vkFreeMemory(device.device(), stagingBufferMemory, nullptr);
    }

    void Texture::createTextureImageView() {
        textureImageView = device.createImageView(textureImage,
                                                     VK_FORMAT_R8G8B8A8_SRGB,
                                                     VK_IMAGE_ASPECT_COLOR_BIT);
    }

    void Texture::createTextureSampler() {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        if (vkCreateSampler(device.device(), &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
            throw std::runtime_error("failed to create texture sampler!");
        }
    }

}  // namespace kc
