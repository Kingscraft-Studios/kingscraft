#pragma once

#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace kc {

    class UiEngine;

    struct Label {
        std::string text;
        std::string fontName = "default";
        float fontSize = 24.0f;
        glm::vec4 color{1.0f};
        bool centered = true;

        float measureWidth(UiEngine& engine) const;

        bool render(UiEngine& engine, glm::vec2 pos, glm::vec2 size,
                     uint32_t elementId, glm::vec4 activeColor) const;

        mutable bool clipReported_ = false;
    };

}
