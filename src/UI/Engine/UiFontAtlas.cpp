#include "UI/Engine/UiFontAtlas.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <fstream>
#include <cstring>
#include <stdexcept>

namespace lve {

    UiFontAtlas::UiFontAtlas(Device& device) : device_(device) {}

    UiFontAtlas::~UiFontAtlas() {
        atlasSampler_.reset();
        atlasView_.reset();
        atlasImage_.reset();
    }

    bool UiFontAtlas::loadFont(const std::string& ttfPath, const std::string& name) {
        std::ifstream file(ttfPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        FontFace face;
        face.fontData.resize(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(face.fontData.data()), size)) {
            return false;
        }

        if (!rasterizeGlyphs(face)) {
            return false;
        }

        fonts_[name] = std::move(face);
        return true;
    }

    bool UiFontAtlas::rasterizeGlyphs(FontFace& face) {
        stbtt_fontinfo info;
        if (!stbtt_InitFont(&info, face.fontData.data(), 0)) return false;
        float scale = stbtt_ScaleForPixelHeight(&info, ATLAS_FONT_SIZE);

        std::vector<unsigned char> atlasPixels(ATLAS_WIDTH * ATLAS_HEIGHT * 4, 0);

        int x = 1, y = 1, rowHeight = 0;

        auto placeGlyph = [&](char32_t codepoint) -> bool {
            int glyphIndex = stbtt_FindGlyphIndex(&info, static_cast<int>(codepoint));
            if (glyphIndex == 0) return false;

            int advanceWidth;
            stbtt_GetGlyphHMetrics(&info, glyphIndex, &advanceWidth, nullptr);

            int ix0, iy0, ix1, iy1;
            stbtt_GetGlyphBitmapBox(&info, glyphIndex, scale, scale, &ix0, &iy0, &ix1, &iy1);

            int gw = ix1 - ix0;
            int gh = iy1 - iy0;

            if (gw <= 0 || gh <= 0) {
                Glyph glyph;
                glyph.uv0 = {0.0f, 0.0f};
                glyph.uv1 = {0.0f, 0.0f};
                glyph.advance = static_cast<float>(advanceWidth) * scale;
                glyph.bearingX = 0.0f;
                glyph.bearingY = 0.0f;
                glyph.width = 0.0f;
                glyph.height = 0.0f;
                face.glyphs[codepoint] = glyph;
                return true;
            }

            if (x + gw + 1 > ATLAS_WIDTH) {
                x = 1;
                y += rowHeight + 1;
                rowHeight = 0;
            }

            if (y + gh + 1 > ATLAS_HEIGHT) return false;

            std::vector<unsigned char> bitmap(static_cast<size_t>(gw) * gh);
            stbtt_MakeGlyphBitmap(&info, bitmap.data(), gw, gh, gw, scale, scale, glyphIndex);

            for (int py = 0; py < gh; py++) {
                for (int px = 0; px < gw; px++) {
                    size_t atlasIdx = static_cast<size_t>((y + py) * ATLAS_WIDTH + (x + px)) * 4;
                    size_t bitmapIdx = static_cast<size_t>(py * gw + px);
                    atlasPixels[atlasIdx + 0] = 255;
                    atlasPixels[atlasIdx + 1] = 255;
                    atlasPixels[atlasIdx + 2] = 255;
                    atlasPixels[atlasIdx + 3] = bitmap[bitmapIdx];
                }
            }

            Glyph glyph;
            glyph.uv0 = { static_cast<float>(x) / ATLAS_WIDTH, static_cast<float>(y) / ATLAS_HEIGHT };
            glyph.uv1 = { static_cast<float>(x + gw) / ATLAS_WIDTH, static_cast<float>(y + gh) / ATLAS_HEIGHT };
            glyph.advance = static_cast<float>(advanceWidth) * scale;
            glyph.bearingX = static_cast<float>(ix0);
            glyph.bearingY = static_cast<float>(iy0);
            glyph.width = static_cast<float>(gw);
            glyph.height = static_cast<float>(gh);

            face.glyphs[codepoint] = glyph;

            x += gw + 1;
            if (gh > rowHeight) rowHeight = gh;
            return true;
        };

        for (char32_t c = 32; c <= 126; c++) {
            placeGlyph(c);
        }

        createAtlasTexture(atlasPixels);
        return true;
    }

    void UiFontAtlas::createAtlasTexture(const std::vector<unsigned char>& pixels) {
        VkDeviceSize imageSize = static_cast<VkDeviceSize>(ATLAS_WIDTH) * ATLAS_HEIGHT * 4;

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        device_.createBuffer(
            imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingBufferMemory);

        void* data;
        vkMapMemory(device_.device(), stagingBufferMemory, 0, imageSize, 0, &data);
        std::memcpy(data, pixels.data(), static_cast<size_t>(imageSize));
        vkUnmapMemory(device_.device(), stagingBufferMemory);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = ATLAS_WIDTH;
        imageInfo.extent.height = ATLAS_HEIGHT;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.flags = 0;

        atlasImage_ = std::make_unique<Image>(device_, imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        device_.transitionImageLayout(
            atlasImage_->getHandle(), VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        device_.copyBufferToImage(stagingBuffer, atlasImage_->getHandle(),
                                   static_cast<uint32_t>(ATLAS_WIDTH),
                                   static_cast<uint32_t>(ATLAS_HEIGHT), 1);

        device_.transitionImageLayout(
            atlasImage_->getHandle(), VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkDestroyBuffer(device_.device(), stagingBuffer, nullptr);
        vkFreeMemory(device_.device(), stagingBufferMemory, nullptr);

        atlasView_ = std::make_unique<ImageView>(
            device_, atlasImage_->getHandle(), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

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

        atlasSampler_ = std::make_unique<Sampler>(device_, samplerInfo);
    }

    const UiFontAtlas::Glyph* UiFontAtlas::getGlyph(const std::string& font, char32_t codepoint) const {
        auto fontIt = fonts_.find(font);
        if (fontIt == fonts_.end()) return nullptr;

        auto glyphIt = fontIt->second.glyphs.find(codepoint);
        if (glyphIt == fontIt->second.glyphs.end()) {
            glyphIt = fontIt->second.glyphs.find(63);
            if (glyphIt == fontIt->second.glyphs.end()) return nullptr;
        }

        return &glyphIt->second;
    }

} // namespace lve
