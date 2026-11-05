#pragma once

#include "Core/Screen.hpp"
#include "Input/ActionRouter.hpp"
#include "Renderer/FrameScene.hpp"
#include "Threads/RenderThread.hpp"
#include "UI/UiWrapper.hpp"
#include "UI/Elements/UiTextBlock.hpp"
#include "UI/Elements/UiButton.hpp"
#include "UI/Elements/UiGradientRect.hpp"
#include "UI/Elements/UiRect.hpp"

namespace kc {

    class MainMenu : public Screen {
    public:
        // The router is injected: MainMenu is created from Kingscraft::start()
        // before Runtime::kingscraft is published, so it must not look the
        // router up through Runtime during init().
        explicit MainMenu(ActionRouter& actions) : actions_(&actions) {}

        void init() override;
        void tick(double) override {}
        void render(FrameScene& scene) override;
        void cleanup() override;

    private:
        void createBackground();
        void createTitle();
        void createButtons();

        // Main menu owns the raw-text UI capture, so text fields/UI key
        // handling work here without an explicit layer toggle.
        ActionRouter* actions_ = nullptr;
        ContextHandle uiContext_;

        UiWrapper& uiSystem_ = Runtime::get().renderThread->getUI();
        VkExtent2D extent_ = Runtime::get().renderThread->getRenderer().getExtent();
        UiGradientRect background_;
        UiRect selectionBarEnter_;
        UiRect selectionBarQuit_;
        UiTextBlock title_;
        UiTextBlock subtitle_;
        UiButton enterWorldButton_;
        UiButton quitButton_;
    };

} // namespace kc
