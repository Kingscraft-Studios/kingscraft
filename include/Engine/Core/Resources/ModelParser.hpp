#pragma once

#include <string>
#include "Core/Resources/BlockModel.hpp"

namespace kc {

class ModelParser {
public:
    struct ParsedQuad {
        FaceDir face = FaceDir::PosY;
        glm::ivec2 uvFrom{0, 0};
        glm::ivec2 uvTo{0, 0};
        int texOrder = 0;
    };
    struct ParsedElement {
        glm::ivec3 from{0};
        glm::ivec3 to{0};
        std::vector<ParsedQuad> quads;
    };
    struct ParsedModel {
        std::vector<ParsedElement> elements;
        std::vector<std::string> texturePaths;
    };

    static ParsedModel parse(const std::vector<char>& jsonBytes);
};

} // namespace kc
