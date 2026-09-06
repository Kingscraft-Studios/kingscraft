#pragma once
#include "IOTemplateBase.hpp"
#include "TextureTemplate.hpp"
#include "Core/Resources/BlockModel.hpp"
#include "Core/Resources/ModelParser.hpp"

namespace kc {
    class ModelTemplate final : public IOTemplateBase {
    public:
        explicit ModelTemplate(IO& io, TextureTemplate& textureTemplate) : IOTemplateBase(io), textureTemplate(textureTemplate) {}

        BlockModel load(const std::string& path) {
            auto jsonBytes = readBytes(path);
            auto parsed = ModelParser::parse(jsonBytes);

            // Decode textures in JSON order, skipping failures. TextureCache builds a
            // flat layer array and skips invalid textures (TextureCache.cpp:51), so the
            // final tileIndex must be resolved after decoding, not during parse.
            std::vector<RawTextureData> textures;
            std::vector<int> orderToFinal(parsed.texturePaths.size(), -1);
            for (size_t i = 0; i < parsed.texturePaths.size(); ++i) {
                auto tex = textureTemplate.loadRaw(parsed.texturePaths[i]);
                if (tex.isValid()) {
                    orderToFinal[i] = static_cast<int>(textures.size());
                    textures.push_back(std::move(tex));
                }
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
                                     q.texOrder < static_cast<int>(orderToFinal.size()) &&
                                     orderToFinal[q.texOrder] >= 0)
                                        ? orderToFinal[q.texOrder] : 0;
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
