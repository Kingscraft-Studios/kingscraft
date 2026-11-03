#include "Vulkan/Texture.hpp"
#include "Vulkan/Buffer.hpp"

#define STB_IMAGE_IMPLEMENTATION

#include <stb_image.h>
#include <stdexcept>

namespace kc {

    Texture::Texture(Device &device, const std::vector<char>& fileData) : device(device) {
        createTextureImage(fileData);
        createTextureImageView();
        createTextureSampler();
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

        // Staging lives only for the upload; the Device copy helper below submits
        // and waits for idle, so it can be released at the end of this scope.
        Buffer staging(
                device,
                imageSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.write(pixels, 0, imageSize);

        stbi_image_free(pixels);

        // Create GPU local image with transfer destination & sampled usage
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {
            static_cast<uint32_t>(texWidth),
            static_cast<uint32_t>(texHeight),
            1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        image_ = std::make_unique<Image>(device, imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        // Transition image layout to receive data
        device.transitionImageLayout(image_->getHandle(),
                                        VK_FORMAT_R8G8B8A8_SRGB,
                                        VK_IMAGE_LAYOUT_UNDEFINED,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Copy buffer (CPU side) to image (GPU side)
        device.copyBufferToImage(staging.getHandle(), image_->getHandle(),
                                    static_cast<uint32_t>(texWidth),
                                    static_cast<uint32_t>(texHeight), 1);

        // Transition image layout for shader read access
        device.transitionImageLayout(image_->getHandle(),
                                        VK_FORMAT_R8G8B8A8_SRGB,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void Texture::createTextureImageView() {
        imageView_ = std::make_unique<ImageView>(
                device, image_->getHandle(),
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

        sampler_ = std::make_unique<Sampler>(device, samplerInfo);
    }

}  // namespace kc
