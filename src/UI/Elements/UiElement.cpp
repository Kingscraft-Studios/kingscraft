#include "UI/Elements/UiElement.hpp"
#include "UI/Engine/UiEngine.hpp"

namespace kc {

    bool UiElement::containsPoint(glm::vec2 point) const {
        if (!visible_) return false;
        glm::vec2 effSize = getInteractionSize();
        glm::vec2 center = position_ + size_ * 0.5f;
        glm::vec2 half = effSize * 0.5f;
        return point.x >= center.x - half.x && point.x <= center.x + half.x &&
               point.y >= center.y - half.y && point.y <= center.y + half.y;
    }

    // A fullscreen quad that is guaranteed to paint every pixel of the frame.
    // The layout test uses the normalized form, so it is correct even before the
    // first resize has run updateLayout(). The colour is read from the style
    // pool, not from color_: ui.vert resolves the style by element id and the
    // vertex colour is unused, so color_ would be the wrong thing to inspect.
    bool UiElement::occludesBelow(const UiEngine& engine) const {
        if (!visible_ || !isFullscreenQuad()) return false;
        if (normalizedPos_ != glm::vec2(0.0f) || pixelOffset_ != glm::vec2(0.0f)) return false;
        if (normalizedSize_ != glm::vec2(1.0f)) return false;
        return engine.isStyleOpaque(getActiveStyleIndex());
    }

    void UiElement::renderLabel(UiEngine& engine, glm::vec4 color) {
        if (label_.text.empty()) return;

        float saved = label_.fontSize;
        label_.fontSize = size_.y * 0.7f;
        float tw = label_.measureWidth(engine);
        if (tw > size_.x && size_.x > 0) {
            label_.fontSize *= size_.x / tw;
        }
        label_.render(engine, position_, size_, elementId_, color);
        label_.fontSize = saved;
    }

} // namespace kc
