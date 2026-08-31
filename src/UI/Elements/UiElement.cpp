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
