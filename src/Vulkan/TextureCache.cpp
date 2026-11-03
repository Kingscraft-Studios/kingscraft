#include "Vulkan/TextureCache.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/FallbackTexture.hpp"
#include "Vulkan/DescriptorPool.hpp"
#include "Vulkan/DescriptorSetLayout.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/Sampler.hpp"
#include "Bus/Message.hpp"
#include "Core/Blocks/Block.hpp"
#include "Core/Registry.hpp"
#include "Core/Resources/BlockModel.hpp"
#include "Util/LogUtils.hpp"
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace kc {

    namespace {

        // One slot per texture the registry declares. A failed slot keeps its
        // position so the mesher's layer index never shifts.
        struct SourceLayer {
            std::vector<unsigned char> pixels;  // RGBA8, empty when this is a fallback
            int width = 0;
            int height = 0;
            std::string path;
            const char* failure = nullptr;
        };

        std::vector<unsigned char> resizeRgba(const std::vector<unsigned char>& src,
                                              int srcW, int srcH, int dstW, int dstH) {
            std::vector<unsigned char> out(static_cast<size_t>(dstW) * dstH * 4);
            for (int y = 0; y < dstH; ++y) {
                const int sy = static_cast<int>(static_cast<int64_t>(y) * srcH / dstH);
                const size_t srcRow = static_cast<size_t>(sy) * srcW;
                const size_t dstRow = static_cast<size_t>(y) * dstW;
                for (int x = 0; x < dstW; ++x) {
                    const int sx = static_cast<int>(static_cast<int64_t>(x) * srcW / dstW);
                    const unsigned char* s = &src[(srcRow + sx) * 4];
                    unsigned char* d = &out[(dstRow + x) * 4];
                    d[0] = s[0];
                    d[1] = s[1];
                    d[2] = s[2];
                    d[3] = s[3];
                }
            }
            return out;
        }

        void warn(const std::string& message) {
            LogUtils::warn(ThreadName::Renderer, "[Textures] " + message);
        }

        void error(const std::string& message) {
            LogUtils::error(ThreadName::Renderer, "[Textures] " + message);
        }

    } // namespace

    // Everything a bound atlas owns. Built aside and swapped in only once every
    // GPU resource exists, so a failed rebuild cannot leave the world with a
    // half-destroyed texture.
    struct TextureCache::Atlas {
        std::unique_ptr<Image> image;
        std::unique_ptr<ImageView> view;
        std::unique_ptr<Sampler> sampler;
        std::unique_ptr<DescriptorSetLayout> layout;
        std::unique_ptr<DescriptorPool> pool;
        VkDescriptorSet set = VK_NULL_HANDLE;

        uint32_t layerCount = 0;
        uint32_t mipLevels = 0;
        int width = 0;
        int height = 0;

        ~Atlas() {
            if (pool && set != VK_NULL_HANDLE) pool->freeSet(set);
            set = VK_NULL_HANDLE;
            pool.reset();
            layout.reset();
            sampler.reset();
            view.reset();
            image.reset();
        }
    };

    TextureCache::TextureCache(Device& device) : device_(device) {}

    TextureCache::~TextureCache() = default;

    VkDescriptorSetLayout TextureCache::getLayout() const {
        return atlas_ && atlas_->layout ? atlas_->layout->getHandle() : VK_NULL_HANDLE;
    }

    VkDescriptorSet TextureCache::getDescriptorSet() const {
        return atlas_ ? atlas_->set : VK_NULL_HANDLE;
    }

    VkImageView TextureCache::getImageView() const {
        return atlas_ && atlas_->view ? atlas_->view->getHandle() : VK_NULL_HANDLE;
    }

    VkSampler TextureCache::getSampler() const {
        return atlas_ && atlas_->sampler ? atlas_->sampler->getHandle() : VK_NULL_HANDLE;
    }

    uint32_t TextureCache::getLayerCount() const {
        return atlas_ ? atlas_->layerCount : 0;
    }

    void TextureCache::updateFromRegistry() {
        auto& registry = Registry<Block>::getRegistry();

        // Decode one layer per declared texture slot, in registry order. Failures
        // keep their slot (instead of being skipped) so the layer indices the
        // mesher computes stay valid and never shift onto a neighbour's texture.
        std::vector<SourceLayer> source;

        registry.forEach([&source](uint64_t, Block& block) {
            const auto& model = block.getModel();
            for (const auto& texData : model.getTextures()) {
                SourceLayer layer;
                layer.path = texData.sourcePath;

                if (!texData.isValid()) {
                    layer.failure = "read failed";
                    source.push_back(std::move(layer));
                    continue;
                }

                int w = 0, h = 0, channels = 0;
                unsigned char* pixels = stbi_load_from_memory(
                    reinterpret_cast<const stbi_uc*>(texData.rawData.data()),
                    static_cast<int>(texData.rawData.size()),
                    &w, &h, &channels, STBI_rgb_alpha);
                if (!pixels || w <= 0 || h <= 0) {
                    if (pixels) stbi_image_free(pixels);
                    layer.failure = "decode failed";
                    source.push_back(std::move(layer));
                    continue;
                }

                layer.width = w;
                layer.height = h;
                layer.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
                stbi_image_free(pixels);
                source.push_back(std::move(layer));
            }
        });

        // Patch per-block texture base offsets so the mesher can compute the
        // global layer. Layer i is now exactly the i-th slot collected above.
        {
            int globalIdx = 0;
            registry.forEach([&globalIdx](uint64_t, Block& b) {
                b.setTextureBaseOffset(globalIdx);
                globalIdx += static_cast<int>(b.getModel().getTextures().size());
            });
        }

        // A VkImage has a single extent, so normalise every layer to the largest
        // declared texture (never to the first one: a 16x16 file must not shrink
        // a 32x32 pack). With nothing decodable, use the fallback tile size.
        int texW = 0, texH = 0;
        for (const auto& layer : source) {
            if (layer.failure) continue;
            texW = std::max(texW, layer.width);
            texH = std::max(texH, layer.height);
        }

        if (source.empty()) {
            // No texture slots at all: keep one stand-in layer so the descriptor
            // set is never empty and the pipeline always has something bound.
            source.emplace_back();
            source.back().failure = "no textures registered";
        }

        if (texW == 0 || texH == 0) texW = texH = FallbackTexture::SIZE;

        const std::vector<unsigned char> fallbackPixels = FallbackTexture::checkerboard(texW, texH);

        int fallbackCount = 0;
        int resizedCount = 0;
        std::vector<std::vector<unsigned char>> layerPixels;
        layerPixels.reserve(source.size());

        for (auto& layer : source) {
            if (layer.failure) {
                layerPixels.push_back(fallbackPixels);
                ++fallbackCount;
                warn((layer.path.empty() ? std::string("texture slot")
                                         : layer.path) +
                     ": " + layer.failure + " -> missing-texture placeholder");
                continue;
            }

            if (layer.width != texW || layer.height != texH) {
                const std::string from = std::to_string(layer.width) + "x" + std::to_string(layer.height);
                const std::string to = std::to_string(texW) + "x" + std::to_string(texH);
                layerPixels.push_back(resizeRgba(layer.pixels, layer.width, layer.height, texW, texH));
                ++resizedCount;
                warn(layer.path + ": " + from + " -> resized to " + to);
            } else {
                layerPixels.push_back(std::move(layer.pixels));
            }
        }

        const uint32_t layerCount = static_cast<uint32_t>(layerPixels.size());
        std::unique_ptr<Atlas> built;

        try {
            built = buildAtlas(layerPixels, texW, texH);
        } catch (const std::runtime_error& e) {
            error(std::string("atlas build failed (") + e.what() + ")");

            if (atlas_) {
                error("keeping previous atlas (" + std::to_string(atlas_->layerCount) +
                      " layers " + std::to_string(atlas_->width) + "x" +
                      std::to_string(atlas_->height) + ")");
                return;
            }

            // Nothing has ever been bound: build a checkerboard atlas with the
            // same layer count so every mesh layer index stays in range.
            warn("no previous atlas, using fallback atlas (" + std::to_string(layerCount) +
                 " layers)");
            const std::vector<std::vector<unsigned char>> allFallback(
                layerCount, FallbackTexture::checkerboard(FallbackTexture::SIZE, FallbackTexture::SIZE));
            try {
                built = buildAtlas(allFallback, FallbackTexture::SIZE, FallbackTexture::SIZE);
            } catch (const std::runtime_error& fatal) {
                // Device lost or out of memory: there is nothing sensible left
                // to render with, and retrying would only hide the failure.
                error(std::string("fallback atlas build failed (") + fatal.what() + ")");
                throw;
            }
            fallbackCount = static_cast<int>(layerCount);
            resizedCount = 0;
        }

        LogUtils::info(ThreadName::Renderer,
            "[Textures] atlas rebuilt: " + std::to_string(built->layerCount) + " layers (" +
            std::to_string(fallbackCount) + " fallback, " + std::to_string(resizedCount) +
            " resized) " + std::to_string(built->width) + "x" + std::to_string(built->height) +
            " mips " + std::to_string(built->mipLevels));

        atlas_ = std::move(built);
    }

    std::unique_ptr<TextureCache::Atlas> TextureCache::buildAtlas(
        const std::vector<std::vector<unsigned char>>& layers, int width, int height) {

        if (layers.empty())
            throw std::runtime_error("no layers to upload");
        if (width <= 0 || height <= 0)
            throw std::runtime_error("invalid atlas extent");

        auto atlas = std::make_unique<Atlas>();
        atlas->layerCount = static_cast<uint32_t>(layers.size());
        atlas->width = width;
        atlas->height = height;
        atlas->mipLevels = static_cast<uint32_t>(
            std::floor(std::log2(static_cast<double>(std::max(width, height))))) + 1;

        const VkDeviceSize layerSize = static_cast<VkDeviceSize>(width) * height * 4;
        const VkDeviceSize totalSize = layerSize * atlas->layerCount;

        // Staging lives only for the upload. The Device helpers below submit and
        // wait for idle, so it can be released when this function returns.
        Buffer staging(
            device_,
            totalSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        for (size_t i = 0; i < layers.size(); ++i) {
            if (layers[i].size() != static_cast<size_t>(layerSize)) {
                // A short layer would read past the end of its vector.
                throw std::runtime_error("layer " + std::to_string(i) + " has " +
                                         std::to_string(layers[i].size()) +
                                         " bytes, expected " + std::to_string(layerSize));
            }
            staging.write(layers[i].data(), static_cast<VkDeviceSize>(i) * layerSize, layerSize);
        }

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = static_cast<uint32_t>(width);
        imageInfo.extent.height = static_cast<uint32_t>(height);
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = atlas->mipLevels;
        imageInfo.arrayLayers = atlas->layerCount;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                          VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        atlas->image = std::make_unique<Image>(device_, imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        // Transition all mip levels to transfer dst.
        device_.transitionImageLayout(atlas->image->getHandle(), VK_FORMAT_R8G8B8A8_SRGB,
                                      VK_IMAGE_LAYOUT_UNDEFINED,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      atlas->layerCount, atlas->mipLevels);

        device_.copyBufferToImage(staging.getHandle(), atlas->image->getHandle(),
                                  static_cast<uint32_t>(width),
                                  static_cast<uint32_t>(height),
                                  atlas->layerCount);

        // Generate the mip chain.
        {
            VkCommandBuffer cmd = device_.beginSingleTimeCommands();

            for (uint32_t mip = 1; mip < atlas->mipLevels; ++mip) {
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = atlas->image->getHandle();
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel = mip - 1;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = atlas->layerCount;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

                const int srcW = std::max(1, width >> (mip - 1));
                const int srcH = std::max(1, height >> (mip - 1));
                const int dstW = std::max(1, width >> mip);
                const int dstH = std::max(1, height >> mip);

                VkImageBlit blit{};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = mip - 1;
                blit.srcSubresource.baseArrayLayer = 0;
                blit.srcSubresource.layerCount = atlas->layerCount;
                blit.srcOffsets[0] = {0, 0, 0};
                blit.srcOffsets[1] = {srcW, srcH, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = mip;
                blit.dstSubresource.baseArrayLayer = 0;
                blit.dstSubresource.layerCount = atlas->layerCount;
                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {dstW, dstH, 1};

                vkCmdBlitImage(cmd,
                    atlas->image->getHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    atlas->image->getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &blit, VK_FILTER_LINEAR);

                barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            // Transition the last mip to shader read.
            VkImageMemoryBarrier lastBarrier{};
            lastBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            lastBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            lastBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            lastBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            lastBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            lastBarrier.image = atlas->image->getHandle();
            lastBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            lastBarrier.subresourceRange.baseMipLevel = atlas->mipLevels - 1;
            lastBarrier.subresourceRange.levelCount = 1;
            lastBarrier.subresourceRange.baseArrayLayer = 0;
            lastBarrier.subresourceRange.layerCount = atlas->layerCount;
            lastBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            lastBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &lastBarrier);

            device_.endSingleTimeCommands(cmd);
        }

        atlas->view = std::make_unique<ImageView>(
            device_, atlas->image->getHandle(), VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_ASPECT_COLOR_BIT,
            atlas->mipLevels, 0,
            VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            atlas->layerCount);

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
        samplerInfo.maxLod = static_cast<float>(atlas->mipLevels);
        samplerInfo.mipLodBias = 0.0f;

        atlas->sampler = std::make_unique<Sampler>(device_, samplerInfo);

        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = 0;
        layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        layoutBinding.descriptorCount = 1;
        layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &layoutBinding;

        atlas->layout = std::make_unique<DescriptorSetLayout>(device_, layoutInfo);

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;

        atlas->pool = std::make_unique<DescriptorPool>(device_, poolInfo);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = atlas->pool->getHandle();
        allocInfo.descriptorSetCount = 1;
        VkDescriptorSetLayout layoutHandle = atlas->layout->getHandle();
        allocInfo.pSetLayouts = &layoutHandle;

        if (vkAllocateDescriptorSets(device_.device(), &allocInfo, &atlas->set) != VK_SUCCESS)
            throw std::runtime_error("failed to allocate texture cache descriptor set!");

        VkDescriptorImageInfo imageInfoDesc{};
        imageInfoDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfoDesc.imageView = atlas->view->getHandle();
        imageInfoDesc.sampler = atlas->sampler->getHandle();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = atlas->set;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfoDesc;

        vkUpdateDescriptorSets(device_.device(), 1, &descriptorWrite, 0, nullptr);

        return atlas;
    }

} // namespace kc
