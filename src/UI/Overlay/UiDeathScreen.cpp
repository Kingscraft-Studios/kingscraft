#include "UI/Overlay/UiDeathScreen.hpp"

#include "Core/Constants.hpp"
#include "UI/UiWrapper.hpp"
#include "UI/Engine/UiStyle.hpp"

namespace lve {

    void UiDeathScreen::init(UiWrapper& ui, float screenW, float screenH) {
        screenW_ = screenW;
        screenH_ = screenH;

        tintStyle_ = ui.registerStyle(UiStyle{
            .mode = RenderMode::Solid,
            .color1 = DEATH_TINT,
        });
        titleStyle_ = ui.registerStyle(UiStyle{
            .mode = RenderMode::Font,
            .color1 = DEATH_RED,
        });
        btnFontStyle_ = ui.registerStyle(UiStyle{
            .mode = RenderMode::Font,
            .color1 = DEATH_RED,
        });
        btnFontHoverStyle_ = ui.registerStyle(UiStyle{
            .mode = RenderMode::Font,
            .color1 = {1.0f, 1.0f, 1.0f, 1.0f},
        });

        tint_.setFillParent();
        tint_.setStyleIndex(tintStyle_);
        tint_.setName("DeathTint");
        tint_.setVisible(false);
        ui.addElement(&tint_);

        buttonBg_.setAnchor({0.5f, 0.5f}, {-140.0f, -25.0f});
        buttonBg_.setSize({280.0f, 50.0f});
        buttonBg_.setStyleIndex(ui.registerStyle(UiStyle{
            .mode = RenderMode::Solid,
            .color1 = BLOOD_DARK_TRANSP,
        }));
        buttonBg_.setName("RespawnButtonBg");
        buttonBg_.setVisible(false);
        ui.addElement(&buttonBg_);

        title_.setAnchor({0.5f, 0.35f}, {0.0f, 0.0f});
        title_.setNormalizedSize({0.6f, 0.12f});
        title_.setText("YOU DIED");
        title_.setFont("default");
        title_.setFontSize(80.0f);
        title_.setColor(DEATH_RED);
        title_.setStyleIndex(titleStyle_);
        title_.setName("DeathTitle");
        title_.setVisible(false);
        ui.addElement(&title_);

        respawnButton_.setAnchor({0.5f, 0.5f}, {-140.0f, -25.0f});
        respawnButton_.setSize({280.0f, 50.0f});
        respawnButton_.setText("RESPAWN");
        respawnButton_.setFont("default");
        respawnButton_.setFontSize(28.0f);
        respawnButton_.setNormalColor(TRANSPARENT);
        respawnButton_.setHoverColor(TRANSPARENT);
        respawnButton_.setNormalTextColor(DEATH_RED);
        respawnButton_.setHoverTextColor({1.0f, 1.0f, 1.0f, 1.0f});
        respawnButton_.setStyleIndex(btnFontStyle_);
        respawnButton_.setHoverStyleIndex(btnFontHoverStyle_);
        respawnButton_.setHandlerName(BTN_RESPAWN);
        respawnButton_.setName("RespawnButton");
        respawnButton_.setVisible(false);
        ui.addElement(&respawnButton_);
    }

    void UiDeathScreen::cleanup(UiWrapper& ui) {
        ui.removeElement(&tint_);
        ui.removeElement(&buttonBg_);
        ui.removeElement(&title_);
        ui.removeElement(&respawnButton_);
    }

    void UiDeathScreen::show(UiWrapper& ui) {
        UiGuard guard(ui);
        tint_.setVisible(true);
        buttonBg_.setVisible(true);
        title_.setVisible(true);
        respawnButton_.setVisible(true);
        tint_.updateLayout(screenW_, screenH_);
        buttonBg_.updateLayout(screenW_, screenH_);
        title_.updateLayout(screenW_, screenH_);
        respawnButton_.updateLayout(screenW_, screenH_);
    }

    void UiDeathScreen::hide(UiWrapper& ui) {
        UiGuard guard(ui);
        tint_.setVisible(false);
        buttonBg_.setVisible(false);
        title_.setVisible(false);
        respawnButton_.setVisible(false);
    }

    void UiDeathScreen::resize(float screenW, float screenH) {
        screenW_ = screenW;
        screenH_ = screenH;
    }

} // namespace lve
