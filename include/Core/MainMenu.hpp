#pragma once

#include "Core/Screen.hpp"
#include "UI/UiWrapper.hpp"
#include "UI/Elements/UiTextBlock.hpp"
#include "UI/Elements/UiButton.hpp"
#include "UI/Elements/UiGradientRect.hpp"
#include "UI/Elements/UiRect.hpp"

namespace lve {

    class MainMenu : public Screen {
    public:
        MainMenu(VkRenderPass renderPass, UiWrapper& uiSystem, VkExtent2D extent);

        void init() override;
        void tick(double) override {}
        void render(const FrameContext& ctx) override { uiSystem_.render(ctx.cmd, ctx.renderPass); }
        void cleanup() override;
        void onRenderPassChanged(VkRenderPass renderPass) override { renderPass_ = renderPass; }
        void onSwapChainRecreated(VkExtent2D extent) override;

        FrameRenderInfo getFrameRenderInfo(const Renderer& renderer, uint32_t) const override;

    private:
        void createBackground();
        void createTitle();
        void createButtons();

        VkRenderPass renderPass_;
        UiWrapper& uiSystem_;
        VkExtent2D extent_;

        UiGradientRect background_;
        UiRect selectionBarEnter_;
        UiRect selectionBarQuit_;
        UiTextBlock title_;
        UiTextBlock subtitle_;
        UiButton enterWorldButton_;
        UiButton quitButton_;
    };

} // namespace lve
