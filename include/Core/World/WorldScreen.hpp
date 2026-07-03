#pragma once

#include "Core/Screen.hpp"
#include "Core/AppContext.hpp"
#include "Core/World/PlayerController.hpp"
#include "Core/World/TerrainRenderer.hpp"
#include "Core/World/World.hpp"
#include "UI/Debug/UiFpsCounter.hpp"
#include <memory>
#include <glm/glm.hpp>

namespace lve {

    class WorldScreen : public Screen {
    public:
        explicit WorldScreen(VkExtent2D extent);
        ~WorldScreen() override;

        void init(const AppContext& ctx) override;
        void tick(double dt) override;
        void render(const FrameContext& ctx) override;
        void renderGlow(const FrameContext& ctx) override;
        void cleanup() override;
        void onRenderPassChanged(VkRenderPass renderPass) override;
        void onSwapChainRecreated(VkExtent2D extent) override;

        FrameRenderInfo getFrameRenderInfo(const Renderer& renderer, uint32_t imageIndex) const override;

    private:
        VkExtent2D extent_{};

        AppContext appCtx_{};
        World* world_ = nullptr;

        PlayerController playerController_;
        TerrainRenderer terrainRenderer_;
        Camera camera_;
        UiFpsCounter fpsCounter_;
    };

} // namespace lve
