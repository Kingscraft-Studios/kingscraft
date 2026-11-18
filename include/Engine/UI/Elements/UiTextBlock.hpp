#pragma once

#include "UI/Elements/UiElement.hpp"

namespace kc {

    class UiTextBlock : public UiElement {
    public:
        UiTextBlock() = default;
        ~UiTextBlock() override = default;

        UiTextBlock(const UiTextBlock&) = delete;
        UiTextBlock& operator=(const UiTextBlock&) = delete;

        void render(UiEngine& engine) override;

        float getTextWidth(UiEngine& engine) const;
    };

} // namespace kc
