#pragma once

#include "UI/Overlay/UiOverlay.hpp"
#include "UI/Elements/UiRect.hpp"
#include "UI/Elements/UiTextBlock.hpp"
#include "UI/Elements/UiButton.hpp"
#include "UI/Elements/UiGroup.hpp"

namespace kc {

    class UiWrapper;

    class UiDeathScreenOverlay : public UiOverlay {
    public:
        UiDeathScreenOverlay() = default;
        ~UiDeathScreenOverlay() = default;

        UiDeathScreenOverlay(const UiDeathScreenOverlay&) = delete;
        UiDeathScreenOverlay& operator=(const UiDeathScreenOverlay&) = delete;

        void init(UiWrapper& ui, float screenW, float screenH) override;
        void cleanup(UiWrapper& ui) override;

        void show(UiWrapper& ui);
        void hide(UiWrapper& ui);

        void resize(float screenW, float screenH) override;

    private:
        float screenW_ = 0.0f;
        float screenH_ = 0.0f;

        UiGroup group_;
        UiRect tint_;
        UiRect buttonBg_;
        UiTextBlock title_;
        UiButton respawnButton_;

        uint32_t tintStyle_ = 0;
        uint32_t titleStyle_ = 0;
        uint32_t btnFontStyle_ = 0;
        uint32_t btnFontHoverStyle_ = 0;
    };

} // namespace kc
