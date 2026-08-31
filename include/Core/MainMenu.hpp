#pragma once

#include "Core/Screen.hpp"
#include "Renderer/FrameScene.hpp"
#include "Threads/Renderer.hpp"
#include "UI/UiWrapper.hpp"
#include "UI/Elements/UiTextBlock.hpp"
#include "UI/Elements/UiButton.hpp"
#include "UI/Elements/UiGradientRect.hpp"
#include "UI/Elements/UiRect.hpp"

namespace kc {

    class MainMenu : public Screen {
    public:
        MainMenu(UiWrapper& uiSystem);

        void init() override;
        void tick(double) override {}
        void render(FrameScene& scene) override;
        void cleanup() override;

    private:
        void createBackground();
        void createTitle();
        void createButtons();

        UiWrapper& uiSystem_;
        VkExtent2D extent_ = RenderThread::getInstance().getRenderer().getExtent();
        UiGradientRect background_;
        UiRect selectionBarEnter_;
        UiRect selectionBarQuit_;
        UiTextBlock title_;
        UiTextBlock subtitle_;
        UiButton enterWorldButton_;
        UiButton quitButton_;
    };

} // namespace kc
