#pragma once

#include "UI/Elements/UiElement.hpp"

namespace kc {

    class UiGradientRect : public UiElement {
    public:
        UiGradientRect() = default;
        ~UiGradientRect() override = default;

        UiGradientRect(const UiGradientRect&) = delete;
        UiGradientRect& operator=(const UiGradientRect&) = delete;

        void render(UiEngine& engine) override;
    };

} // namespace kc
