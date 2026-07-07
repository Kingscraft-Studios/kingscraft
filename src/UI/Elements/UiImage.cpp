#include "UI/Elements/UiImage.hpp"
#include "UI/Engine/UiEngine.hpp"

namespace lve {

    void UiImage::render(UiEngine& engine) {
        if (!visible_) return;

        float x0 = position_.x;
        float y0 = position_.y;
        float x1 = position_.x + size_.x;
        float y1 = position_.y + size_.y;

        UiVertex verts[4] = {
            {{x0, y0}, {0.0f, 0.0f}, color_, elementId_},
            {{x1, y0}, {1.0f, 0.0f}, color_, elementId_},
            {{x1, y1}, {1.0f, 1.0f}, color_, elementId_},
            {{x0, y1}, {0.0f, 1.0f}, color_, elementId_},
        };

        uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
        engine.addQuad(verts, indices);
    }

} // namespace lve
