// TODO: SSBO Style System — MainMenu redesign
// Uses registered styles with the SSBO system:
// - Solid background via UiGradientRect (setFillParent)
// - Gold text with Arial font, anchored positions
// - Buttons with transparent bg, gold text, gold selection bar
// - Hover styles for text color change (gold_dark → gold)
// - Selection bar rects alongside buttons
// - Anchor system repositioning on resize via UiWrapper

#include "Core/MainMenu.hpp"

#include "Core/World/WorldScreen.hpp"
#include "Core/Constants.hpp"
#include "Renderer/Renderer.hpp"

namespace lve {

    // Shared style indices for all MainMenu instances
    static uint32_t g_styleBg = 0;
    static uint32_t g_styleTitle = 0;
    static uint32_t g_styleSubtitle = 0;
    static uint32_t g_styleBtnText = 0;
    static uint32_t g_styleBtnTextHover = 0;
    static uint32_t g_styleSelBar = 0;
    static bool g_stylesInit = false;

    static void ensureStyles(UiWrapper& ui) {
        if (g_stylesInit) return;
        g_styleBg = ui.registerStyle(UiStyle{
            .mode = RenderMode::Solid,
            .color1 = BG_DARK,
        });
        g_styleTitle = ui.registerStyle(UiStyle{
            .mode = RenderMode::Font,
            .color1 = GOLD
        });
        g_styleSubtitle = ui.registerStyle(UiStyle{
            .mode = RenderMode::Font,
            .color1 = GOLD_DARK
        });
        g_styleBtnText = ui.registerStyle(UiStyle{
            .mode = RenderMode::Font,
            .color1 = GOLD_DARK
        });
        g_styleBtnTextHover = ui.registerStyle(UiStyle{
            .mode = RenderMode::Font,
            .color1 = GOLD
        });
        g_styleSelBar = ui.registerStyle(UiStyle{
            .mode = RenderMode::Solid,
            .color1 = GOLD
        });
        g_stylesInit = true;
    }

    MainMenu::MainMenu(VkRenderPass renderPass, UiWrapper& uiSystem, VkExtent2D extent)
        : renderPass_(renderPass), uiSystem_(uiSystem), extent_(extent) {
    }

    void MainMenu::init() {
        ensureStyles(uiSystem_);
        createBackground();
        createTitle();
        createButtons();
        // Apply initial layout
        uiSystem_.resize(static_cast<int>(extent_.width),
                         static_cast<int>(extent_.height));
    }

    void MainMenu::cleanup() {
        uiSystem_.removeElement(&background_);
        uiSystem_.removeElement(&title_);
        uiSystem_.removeElement(&subtitle_);
        uiSystem_.removeElement(&selectionBarEnter_);
        uiSystem_.removeElement(&selectionBarQuit_);
        uiSystem_.removeElement(&enterWorldButton_);
        uiSystem_.removeElement(&quitButton_);
    }

    void MainMenu::onSwapChainRecreated(VkExtent2D extent) {
        extent_ = extent;
    }

    void MainMenu::createBackground() {
        background_.setFillParent();
        background_.setStyleIndex(g_styleBg);
        background_.setName("BgGradient");
        uiSystem_.addElement(&background_);
    }

    void MainMenu::createTitle() {
        title_.setAnchor({0.5f, 0.0f}, {-300.0f, 100.0f});
        title_.setNormalizedSize({0.3125f, 0.0648f});
        title_.setText("KINGSCRAFT");
        title_.setFont("default");
        title_.setFontSize(64.0f);
        title_.setColor(GOLD);
        title_.setStyleIndex(g_styleTitle);
        title_.setName("Title");
        uiSystem_.addElement(&title_);

        subtitle_.setAnchor({0.5f, 0.0f}, {-200.0f, 170.0f});
        subtitle_.setNormalizedSize({0.2083f, 0.0278f});
        subtitle_.setText("THE SANDBOX WITHOUT LIMITS");
        subtitle_.setFont("default");
        subtitle_.setFontSize(18.0f);
        subtitle_.setColor(GOLD_DARK);
        subtitle_.setStyleIndex(g_styleSubtitle);
        subtitle_.setName("Subtitle");
        uiSystem_.addElement(&subtitle_);
    }

    void MainMenu::createButtons() {
        // Anchor origin: left edge, 45% down the screen
        // Pixel offsets position each element relative to that anchor

        // "ENTER WORLD" button
        enterWorldButton_.setAnchor({0.0f, 0.45f}, {140.0f, 0.0f});
        enterWorldButton_.setNormalizedSize({0.15625f, 0.0463f});
        enterWorldButton_.setText("ENTER WORLD");
        enterWorldButton_.setFont("default");
        enterWorldButton_.setNormalColor(TRANSPARENT);
        enterWorldButton_.setHoverColor(TRANSPARENT);
        enterWorldButton_.setNormalTextColor(GOLD_DARK);
        enterWorldButton_.setHoverTextColor(GOLD);
        enterWorldButton_.setStyleIndex(g_styleBtnText);
        enterWorldButton_.setHoverStyleIndex(g_styleBtnTextHover);
        enterWorldButton_.setHandlerName(BTN_ENTER_WORLD);
        enterWorldButton_.setName("EnterWorldButton");
        uiSystem_.addElement(&enterWorldButton_);

        // Selection bar for "ENTER WORLD" — 20px left of button, vertically centered (50-40)/2=5
        selectionBarEnter_.setAnchor({0.0f, 0.45f}, {120.0f, 5.0f});
        selectionBarEnter_.setNormalizedSize({0.0021f, 0.0370f});
        selectionBarEnter_.setColor(GOLD);
        selectionBarEnter_.setStyleIndex(g_styleSelBar);
        selectionBarEnter_.setName("SelBarEnter");
        uiSystem_.addElement(&selectionBarEnter_);

        // "QUIT GAME" button — 60px below row 1 (50px button + 10px spacing)
        quitButton_.setAnchor({0.0f, 0.45f}, {140.0f, 60.0f});
        quitButton_.setNormalizedSize({0.15625f, 0.0463f});
        quitButton_.setText("QUIT GAME");
        quitButton_.setFont("default");
        quitButton_.setNormalColor(TRANSPARENT);
        quitButton_.setHoverColor(TRANSPARENT);
        quitButton_.setNormalTextColor(GOLD_DARK);
        quitButton_.setHoverTextColor(GOLD);
        quitButton_.setStyleIndex(g_styleBtnText);
        quitButton_.setHoverStyleIndex(g_styleBtnTextHover);
        quitButton_.setHandlerName(BTN_QUIT_GAME);
        quitButton_.setName("QuitButton");
        uiSystem_.addElement(&quitButton_);

        // Selection bar for "QUIT GAME" — 20px left of button, 65px from anchor (60+5)
        selectionBarQuit_.setAnchor({0.0f, 0.45f}, {120.0f, 65.0f});
        selectionBarQuit_.setNormalizedSize({0.0021f, 0.0370f});
        selectionBarQuit_.setColor(GOLD);
        selectionBarQuit_.setStyleIndex(g_styleSelBar);
        selectionBarQuit_.setName("SelBarQuit");
        uiSystem_.addElement(&selectionBarQuit_);
    }

    FrameRenderInfo MainMenu::getFrameRenderInfo(const Renderer& renderer, uint32_t) const {
        return {
            renderPass_,
            renderer.getCurrentFramebuffer(),
            {{{0.1f, 0.1f, 0.1f, 1.0f}}},
            true
        };
    }
} // namespace lve
