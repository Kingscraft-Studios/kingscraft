#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include "BaseThread.hpp"
#include "Bus/Mailbox.hpp"
#include "Core/ScreenManager.hpp"
#include "Core/World/Biomes/BiomeProvider.hpp"
#include "Core/World/TerrainGenerator.hpp"
#include "Core/World/World.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Util/TimeUtil.hpp"

namespace kc {

class Kingscraft : public BaseThread{
public:

    void start() override;
    void run() override;
    void stop() override;
    void signalQuit() override;

    // Public Getters
    ScreenManager& getScreenManager() { return *screenManager; }
    World& getWorld() { return *world;}
    template<typename T, typename... Args>
    void setScreen(Args&&... args) {
        screenManager->setScreen<T>(std::forward<Args>(args)...);
    }

    double getDelta() { return dt_; }

    // Biome encoding at the player's feet, updated each tick on this thread,
    // read atomically from the renderer thread (UiDebugOverlay).
    uint64_t getCurrentBiome() { return currentBiomeEncoded_.load(std::memory_order_relaxed); }

    // FLY state for the debug overlay (double-jump fly mode).
    bool isPlayerFlying() { return playerFlying_.load(std::memory_order_relaxed); }

private:
    void tick();
    void registerAllKeys();
    void registerUICallbacks();

    std::shared_ptr<Mailbox> mailbox_;

    double prevTime_ = 0.0;
    double dt_ = 0.0;
    double tickAccumulator_ = 0.0;
    double cpuTickMs_ = 0.0;

    static constexpr double TICK_RATE = 100.0;
    static constexpr double TICK_INTERVAL = 1.0 / TICK_RATE;

    std::unique_ptr<ScreenManager> screenManager = std::make_unique<ScreenManager>();
    // Built from the fresh WorldMetadata's settings; World re-seeds it via
    // ITerrainGenerator::applySettings once the loaded world.kcw resolves.
    //
    // The generator owns its BiomeProvider, because the provider has to share
    // the generator's HomelandsNoise: two separately seeded graphs would let the
    // biome map and the heightfield disagree. Read the provider through
    // getBiomeProvider() rather than caching a reference, since applySettings()
    // replaces the instance.
    std::unique_ptr<DefaultTerrainGenerator> terrainGen;
    std::unique_ptr<World> world;

    std::atomic<uint64_t> currentBiomeEncoded_{0};
    std::atomic<bool> playerFlying_{false};
};

}
