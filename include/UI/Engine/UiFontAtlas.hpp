#pragma once

#include "Vulkan/Device.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/Sampler.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace lve {

    class UiFontAtlas {
    public:
        static constexpr int ATLAS_WIDTH = 1024;
        static constexpr int ATLAS_HEIGHT = 1024;
        static constexpr float ATLAS_FONT_SIZE = 64.0f;

        struct Glyph {
            glm::vec2 uv0, uv1;
            float advance;
            float bearingX;
            float bearingY;
            float width;
            float height;
        };

        UiFontAtlas(Device& device);
        ~UiFontAtlas();

        UiFontAtlas(const UiFontAtlas&) = delete;
        UiFontAtlas& operator=(const UiFontAtlas&) = delete;

        bool loadFont(const std::string& ttfPath, const std::string& name);

        const Glyph* getGlyph(const std::string& font, char32_t codepoint) const;

        VkImageView getImageView() const { return atlasView_ ? atlasView_->getHandle() : VK_NULL_HANDLE; }
        VkSampler getSampler() const { return atlasSampler_ ? atlasSampler_->getHandle() : VK_NULL_HANDLE; }
        bool isReady() const { return atlasImage_ != nullptr; }

    private:
        struct FontFace {
            std::vector<unsigned char> fontData;
            std::unordered_map<char32_t, Glyph> glyphs;
        };

        bool rasterizeGlyphs(FontFace& face);
        void createAtlasTexture(const std::vector<unsigned char>& pixels);

        Device& device_;
        std::unordered_map<std::string, FontFace> fonts_;

        std::unique_ptr<Image> atlasImage_;
        std::unique_ptr<ImageView> atlasView_;
        std::unique_ptr<Sampler> atlasSampler_;
    };

} // namespace lve
