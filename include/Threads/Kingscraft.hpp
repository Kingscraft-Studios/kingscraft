#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include "BaseThread.hpp"
#include "Bus/Mailbox.hpp"
#include "Core/ScreenManager.hpp"
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
    // TODO: Add a Enum Class to Differ Different TerrainGenerators!
    DefaultTerrainGenerator terrainGen;
    std::unique_ptr<World> world;
};

}
