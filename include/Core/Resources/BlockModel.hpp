#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace kc {

    struct DecodedTextureData {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;

        bool isValid() const { return !pixels.empty(); }
    };

struct RawTextureData {
    std::vector<unsigned char> rawData;
    int width = 0;
    int height = 0;

    bool isValid() const { return !rawData.empty(); }
};

enum class FaceDir : uint8_t { PosX, NegX, PosY, NegY, PosZ, NegZ };

struct Quad {
    FaceDir face;
    glm::ivec2 uvFrom;
    glm::ivec2 uvTo;
    int tileIndex = 0;
};

struct Element {
    glm::ivec3 from;
    glm::ivec3 to;
    std::vector<Quad> quads;
};

class BlockModel {
    std::vector<Element> elements_;
    std::vector<RawTextureData> textures_;
public:
    BlockModel() = default;
    BlockModel(std::vector<Element> elements, std::vector<RawTextureData> textures)
        : elements_(std::move(elements)), textures_(std::move(textures)) {}

    const auto& getElements() const { return elements_; }
    const auto& getTextures() const { return textures_; }

    const Quad* findQuad(FaceDir dir) const {
        for (const auto& el : elements_)
            for (const auto& q : el.quads)
                if (q.face == dir) return &q;
        return nullptr;
    }

    int addTexture(RawTextureData tex) {
        int idx = static_cast<int>(textures_.size());
        textures_.push_back(std::move(tex));
        return idx;
    }
};

} // namespace kc
