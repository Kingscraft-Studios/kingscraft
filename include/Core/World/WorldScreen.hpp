#pragma once

#include "Core/Screen.hpp"
#include "Core/World/PlayerController.hpp"
#include "Core/World/TerrainRenderer.hpp"
#include "Core/World/World.hpp"
#include "UI/Debug/UiFpsCounter.hpp"
#include "UI/Overlay/UiHotbar.hpp"
#include <memory>
#include <glm/glm.hpp>

namespace lve {

    class WorldScreen : public Screen {
    public:
        explicit WorldScreen();
        ~WorldScreen() override;

        void init() override;
        void tick(double dt) override;
        void render(const FrameContext& ctx) override;
        void renderGlow(const FrameContext& ctx) override;
        void cleanup() override;
        void onMouseButton(int button, int action, int mods) override;
        void onRenderPassChanged(VkRenderPass renderPass) override;
        void onSwapChainRecreated(VkExtent2D extent) override;

    private:
        VkExtent2D extent_{};

        PlayerController playerController_;
        TerrainRenderer terrainRenderer_;
        Camera camera_;
        UiFpsCounter fpsCounter_;
        bool wasDebugOn_ = false;
        UiHotbar hotbar_;
    };

} // namespace lve
