#pragma once

#include "Core/Screen.hpp"
#include "Core/World/TerrainRenderer.hpp"
#include "Core/World/World.hpp"
#include "UI/Overlay/UiDebugOverlay.hpp"
#include "UI/Overlay/UiHotbarOverlay.hpp"
#include "UI/Overlay/UiDeathScreenOverlay.hpp"
#include "UI/Elements/UiRect.hpp"
#include "Event/EventSubscription.hpp"
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

        // Rebuilds the hotbar icons after a registry reload changed the
        // block textures (called from the GameLogic thread).
        void refreshHotbar();

        // Driven by PlayerLifecycleListener when the player dies/respawns;
        // owns the cursor + UI-layer + death-overlay transition (GameLogic thread).
        void onPlayerDeath();
        void onPlayerRespawn();
    private:
        VkExtent2D extent_{};

        TerrainRenderer terrainRenderer_;
        // Shared: the MetricsUpdateEvent listener keeps it alive while an event
        // is in flight to the renderer thread, even after this screen is torn
        // down (cleanup() flips it inactive so late events are no-ops).
        std::shared_ptr<UiDebugOverlay> debugOverlay_;
        EventSubscription debugOverlaySub_;
        bool wasDebugOn_ = false;
        UiHotbarOverlay hotbar_;
        UiDeathScreenOverlay deathScreen_;
        UiRect crosshairH_;
        UiRect crosshairV_;
        uint32_t crosshairStyle_ = 0;
    };

} // namespace kc
