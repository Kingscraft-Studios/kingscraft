#include "Vulkan/FallbackTexture.hpp"
#include "Vulkan/Buffer.hpp"

#include <algorithm>
#include <cstdint>

namespace kc {

    std::vector<unsigned char> FallbackTexture::checkerboard(int w, int h) {
        if (w <= 0 || h <= 0) return {};

        const int cell = std::max(1, std::min(w, h) / 2);
        std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const bool magenta = ((x / cell) + (y / cell)) % 2 == 0;
                unsigned char* p = &pixels[(static_cast<size_t>(y) * w + x) * 4];
                p[0] = magenta ? 255 : 0;   // R
                p[1] = 0;                   // G
                p[2] = magenta ? 255 : 0;   // B
                p[3] = 255;                 // A
            }
        }
        return pixels;
    }

    VkSamplerCreateInfo FallbackTexture::samplerInfo(uint32_t mipLevels) {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_NEAREST;
        info.minFilter = VK_FILTER_NEAREST;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.anisotropyEnable = VK_FALSE;
        info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
        info.unnormalizedCoordinates = VK_FALSE;
        info.compareEnable = VK_FALSE;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.minLod = 0.0f;
        info.maxLod = mipLevels > 0 ? static_cast<float>(mipLevels - 1) : 0.0f;
        return info;
    }

    FallbackTexture::FallbackTexture(Device& device) {
        constexpr uint32_t mipLevels = 1;

        const std::vector<unsigned char> pixels = checkerboard(SIZE, SIZE);
        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(pixels.size());

        // Staging lives only for the upload. The Device helpers below submit and
        // wait for idle, so it can be released when this constructor returns.
        Buffer staging(
            device,
            imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.write(pixels.data(), 0, imageSize);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = SIZE;
        imageInfo.extent.height = SIZE;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        image_ = std::make_unique<Image>(device, imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        device.transitionImageLayout(
            image_->getHandle(),
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        device.copyBufferToImage(staging.getHandle(), image_->getHandle(), SIZE, SIZE, 1);

        device.transitionImageLayout(
            image_->getHandle(),
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // A one-layer array view, matching the block atlas this stands in for.
        view_ = std::make_unique<ImageView>(
            device,
            image_->getHandle(),
            VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_ASPECT_COLOR_BIT,
            mipLevels,
            0,
            VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            1);

        sampler_ = std::make_unique<Sampler>(device, samplerInfo(mipLevels));
    }

} // namespace kc
