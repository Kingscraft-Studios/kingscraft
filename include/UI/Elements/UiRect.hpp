#pragma once

#include "UI/Elements/UiElement.hpp"

namespace kc {

    class UiRect : public UiElement {
    public:
        UiRect() = default;
        ~UiRect() override = default;

        UiRect(const UiRect&) = delete;
        UiRect& operator=(const UiRect&) = delete;

        void render(UiEngine& engine) override;

        // A plain rect can cover the frame; whether it actually does depends on
        // layout + style opacity, which occludesBelow() checks.
        bool isFullscreenQuad() const override { return true; }
    };

} // namespace kc
