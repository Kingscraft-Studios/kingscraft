#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace kc {

struct TextureData {
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
    std::vector<TextureData> textures_;
public:
    BlockModel() = default;
    BlockModel(std::vector<Element> elements, std::vector<TextureData> textures)
        : elements_(std::move(elements)), textures_(std::move(textures)) {}

    const auto& getElements() const { return elements_; }
    const auto& getTextures() const { return textures_; }

    int addTexture(TextureData tex) {
        int idx = static_cast<int>(textures_.size());
        textures_.push_back(std::move(tex));
        return idx;
    }
};

} // namespace kc
