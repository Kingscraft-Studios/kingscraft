#pragma once

#include "UI/Elements/UiRect.hpp"
#include "UI/Elements/UiTextBlock.hpp"
#include "UI/Elements/UiButton.hpp"

namespace lve {

    class UiWrapper;

    class UiDeathScreen {
    public:
        UiDeathScreen() = default;
        ~UiDeathScreen() = default;

        UiDeathScreen(const UiDeathScreen&) = delete;
        UiDeathScreen& operator=(const UiDeathScreen&) = delete;

        void init(UiWrapper& ui, float screenW, float screenH);
        void cleanup(UiWrapper& ui);

        void show(UiWrapper& ui);
        void hide(UiWrapper& ui);

        void resize(float screenW, float screenH);

    private:
        float screenW_ = 0.0f;
        float screenH_ = 0.0f;

        UiRect tint_;
        UiRect buttonBg_;
        UiTextBlock title_;
        UiButton respawnButton_;

        uint32_t tintStyle_ = 0;
        uint32_t titleStyle_ = 0;
        uint32_t btnFontStyle_ = 0;
        uint32_t btnFontHoverStyle_ = 0;
    };

} // namespace lve
