#include "UI/Elements/UiButton.hpp"
#include "UI/Engine/UiEngine.hpp"

namespace kc {

    void UiButton::render(UiEngine& engine) {
        if (!visible_) return;

        // Background
        glm::vec4 bgColor = isHovered_ ? hoverColor_ : normalColor_;
        float x0 = position_.x;
        float y0 = position_.y;
        float x1 = position_.x + size_.x;
        float y1 = position_.y + size_.y;

        UiVertex bgVerts[4] = {
            {{x0, y0}, {-1.0f, -1.0f}, bgColor, elementId_},
            {{x1, y0}, {-1.0f, -1.0f}, bgColor, elementId_},
            {{x1, y1}, {-1.0f, -1.0f}, bgColor, elementId_},
            {{x0, y1}, {-1.0f, -1.0f}, bgColor, elementId_},
        };
        uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
        engine.addQuad(bgVerts, indices);

        // Text
        if (label_.text.empty()) return;

        renderLabel(engine, getActiveTextColor());
    }

    bool UiButton::containsPoint(glm::vec2 point) const {
        return UiElement::containsPoint(point);
    }

    void UiButton::click() {
        if (onClick_) onClick_();
    }

} // namespace kc
