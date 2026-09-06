#pragma once
#include "IOTemplateBase.hpp"
#include "stb_image.h"
#include "Core/Resources/BlockModel.hpp"

namespace kc {
    class TextureTemplate final : public IOTemplateBase {
    public:
        explicit TextureTemplate(IO& io) : IOTemplateBase(io) {}

        RawTextureData loadRaw(const std::string& path) {
            auto bytes = readBytes(path);
            RawTextureData tex;
            if (bytes.empty()) return tex;

            tex.rawData.assign(bytes.begin(), bytes.end());

            int w, h, ch = 0;

            if (stbi_info_from_memory(tex.rawData.data(), static_cast<int>(tex.rawData.size()), &w, &h, &ch)) {
                tex.width = w;
                tex.height = h;
            }

            return tex;
        }

        DecodedTextureData loadDecoded(const std::string& path) {
            auto bytes = readBytes(path);
            DecodedTextureData  tex;

            if (bytes.empty()) return tex;

            int w, h, ch = 0;

            unsigned char* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &ch, 4);

            if (!pixels) return tex;

            tex.width = w;
            tex.height = h;
            tex.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);

            stbi_image_free(pixels);
            return tex;
        }
    };
}
