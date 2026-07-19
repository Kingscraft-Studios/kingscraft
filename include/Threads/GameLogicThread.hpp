#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include "Bus/Mailbox.hpp"
#include "Util/TimeUtil.hpp"

namespace lve {

class GameLogicThread {
public:
    static GameLogicThread& getInstance() {
        static GameLogicThread instance;
        return instance;
    }

    void init() { prevTime_ = TimeUtil::uptimeSeconds(); }
    void run();
    void stop();

    bool isTickReady() const;
    void ackTick();
    double getCpuTickMs() const;
    double getDt() const;

private:
    void tick();

    std::atomic<bool> running_{true};
    std::shared_ptr<Mailbox> mailbox_;
    std::atomic<bool> tickReady_{false};

    double prevTime_ = 0.0;
    double dt_ = 0.0;
    double tickAccumulator_ = 0.0;
    double cpuTickMs_ = 0.0;

    static constexpr double TICK_RATE = 100.0;
    static constexpr double TICK_INTERVAL = 1.0 / TICK_RATE;
};

}
