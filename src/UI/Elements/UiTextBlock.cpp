#include "UI/Elements/UiTextBlock.hpp"
#include "UI/Engine/UiEngine.hpp"

namespace kc {

    void UiTextBlock::render(UiEngine& engine) {
        if (!visible_ || label_.text.empty()) return;

        renderLabel(engine, color_);
    }

    float UiTextBlock::getTextWidth(UiEngine& engine) const {
        return label_.measureWidth(engine);
    }

} // namespace kc
