#include "UI/Elements/UiGradientRect.hpp"
#include "UI/Engine/UiEngine.hpp"

namespace kc {

    void UiGradientRect::render(UiEngine& engine) {
        if (!visible_) return;

        float x0 = position_.x;
        float y0 = position_.y;
        float x1 = position_.x + size_.x;
        float y1 = position_.y + size_.y;

        // uv encodes the gradient direction/center in UV space (0-1 across rect)
        glm::vec2 uv0{0.0f, 0.0f};
        glm::vec2 uv1{1.0f, 1.0f};

        UiVertex verts[4] = {
            {{x0, y0}, uv0, color_, elementId_},
            {{x1, y0}, {uv1.x, uv0.y}, color_, elementId_},
            {{x1, y1}, uv1, color_, elementId_},
            {{x0, y1}, {uv0.x, uv1.y}, color_, elementId_},
        };

        uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
        engine.addQuad(verts, indices);
    }

} // namespace kc
