#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include "Bus/Mailbox.hpp"
#include "Core/ScreenManager.hpp"
#include "Core/World/TerrainGenerator.hpp"
#include "Core/World/World.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Util/TimeUtil.hpp"

namespace kc {

class Kingscraft {
public:
    static Kingscraft& getInstance() {
        static Kingscraft instance;
        return instance;
    }

    void init();
    void run();
    void stop();

    // Public Getters
    ScreenManager& getScreenManager() { return *screenManager; }
    World& getWorld() { return *world;}
    template<typename T, typename... Args>
    void setScreen(Args&&... args) {
        screenManager->setScreen<T>(std::forward<Args>(args)...);
    }

    double getDelta() { return dt_; }

private:
    void tick();
    void registerAllKeys();
    void registerUICallbacks();

    std::atomic<bool> running_{true};
    std::shared_ptr<Mailbox> mailbox_;

    double prevTime_ = 0.0;
    double dt_ = 0.0;
    double tickAccumulator_ = 0.0;
    double cpuTickMs_ = 0.0;

    static constexpr double TICK_RATE = 100.0;
    static constexpr double TICK_INTERVAL = 1.0 / TICK_RATE;

    std::unique_ptr<ScreenManager> screenManager = std::make_unique<ScreenManager>();
    DefaultTerrainGenerator terrainGen;
    std::unique_ptr<World> world;
};

}
