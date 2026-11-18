#pragma once
#include "IOTemplateBase.hpp"
#include "TextureTemplate.hpp"
#include "Core/Resources/BlockModel.hpp"
#include "Core/Resources/ModelParser.hpp"

namespace kc {
    class ModelTemplate final : public IOTemplateBase {
    public:
        explicit ModelTemplate(DiskLayer& disk, TextureTemplate& textureTemplate) : IOTemplateBase(disk), textureTemplate(textureTemplate) {}

        BlockModel load(const std::string& path) {
            auto jsonBytes = readBytes(path, MissingFile::Warn);
            if (!jsonBytes) return BlockModel{};

            auto parsed = ModelParser::parse(*jsonBytes);

            // One slot per JSON texture path, in JSON order. A failed load stays
            // as an empty slot instead of being removed: the atlas builder turns
            // empty slots into the global missing-texture checkerboard, and every
            // quad's texOrder is already its final layer offset within the model.
            // Dropping failed entries used to shift all later layers and make a
            // missing file render as a different texture.
            std::vector<RawTextureData> textures;
            textures.reserve(parsed.texturePaths.size());
            for (const auto& texturePath : parsed.texturePaths) {
                textures.push_back(textureTemplate.loadRaw(texturePath));
            }

            // Convert parsed elements -> real Elements, patching texOrder -> tileIndex.
            std::vector<Element> elements;
            elements.reserve(parsed.elements.size());
            for (auto& el : parsed.elements) {
                Element e;
                e.from = el.from;
                e.to = el.to;
                e.quads.reserve(el.quads.size());
                for (auto& q : el.quads) {
                    Quad out;
                    out.face = q.face;
                    out.uvFrom = q.uvFrom;
                    out.uvTo = q.uvTo;
                    out.tileIndex = (q.texOrder >= 0 &&
                                     q.texOrder < static_cast<int>(textures.size()))
                                        ? q.texOrder : 0;
                    e.quads.push_back(out);
                }

                elements.push_back(std::move(e));
            }

            return BlockModel(std::move(elements), std::move(textures));
        }

    private:
        TextureTemplate& textureTemplate;
    };
}
