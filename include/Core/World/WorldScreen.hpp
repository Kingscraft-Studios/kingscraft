#pragma once

#include "Core/Screen.hpp"
#include "Core/World/TerrainRenderer.hpp"
#include "Core/World/World.hpp"
#include "UI/Debug/UiFpsCounter.hpp"
#include "UI/Overlay/UiHotbar.hpp"
#include "UI/Overlay/UiDeathScreen.hpp"
#include "UI/Elements/UiRect.hpp"
#include <memory>
#include <glm/glm.hpp>

namespace kc {

    class WorldScreen : public Screen {
    public:
        explicit WorldScreen();
        ~WorldScreen() override;

        void init() override;
        void tick(double dt) override;
        void render(FrameScene& scene) override;
        void cleanup() override;
        void onMouseButton(int button, int action, int mods) override;
    private:
        VkExtent2D extent_{};

        TerrainRenderer terrainRenderer_;
        UiFpsCounter fpsCounter_;
        bool wasDebugOn_ = false;
        bool wasDead_ = false;
        UiHotbar hotbar_;
        UiDeathScreen deathScreen_;
        UiRect crosshairH_;
        UiRect crosshairV_;
        uint32_t crosshairStyle_ = 0;
    };

} // namespace kc
