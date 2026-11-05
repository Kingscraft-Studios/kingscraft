#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include "BaseThread.hpp"
#include "Bus/Mailbox.hpp"
#include "Core/Observability/Metrics/MetricsTypes.hpp"
#include "Core/ScreenManager.hpp"
#include "Core/World/Biomes/BiomeProvider.hpp"
#include "Core/World/TerrainGenerator.hpp"
#include "Core/World/World.hpp"
#include "Core/World/WorldManager.hpp"
#include "Input/ActionRouter.hpp"
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
    WorldManager& getWorldManager() { return worldManager; }

    // GameLogic-only action dispatcher. Input actions arrive here as POD
    // events; handlers always run on this thread.
    ActionRouter& getActionRouter() { return actions_; }

    // Temporary Compatibility Accessor
    World& getWorld() { return *worldManager.getWorld(); }

    template<typename T, typename... Args>
    void setScreen(Args&&... args) {
        screenManager->setScreen<T>(std::forward<Args>(args)...);
    }

    double getDelta() { return dt_; }

    // Game-logic metrics for the current tick. Written by tick()/WorldScreen and
    // handed to Observability at the end of the tick; kept here so the render
    // thread never touches it directly.
    GameLogicMetrics& getCPUMetrics() { return cpu_; }

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

    WorldManager worldManager;

    // Declared before the handles/subscriptions so they are destroyed first
    // (their RAII cleanup can still reach a live router/registry).
    ActionRouter actions_;
    ContextHandle globalContext_;
    ActionRouter::Subscription fullscreenSub_;
    ActionRouter::Subscription debugSub_;
    ActionRouter::Subscription logElementSub_;
    ActionRouter::Subscription profilerSub_;
    ActionRouter::Subscription reloadSub_;
    ActionRouter::Subscription dumpSub_;

    std::atomic<uint64_t> currentBiomeEncoded_{0};
    std::atomic<bool> playerFlying_{false};

    GameLogicMetrics cpu_;
};

}
