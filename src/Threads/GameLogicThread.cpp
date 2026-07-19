#include "Threads/GameLogicThread.hpp"

#include "Bus/MessageBus.hpp"
#include "Threads/Engine.hpp"
#include "Vulkan/App.hpp"
#include "Util/TimeUtil.hpp"
#include "Util/LogUtils.hpp"

namespace lve {

void GameLogicThread::run()
{
    mailbox_ = std::make_shared<Mailbox>();
    MessageBus::Get().subscribe(ThreadName::GameLogic, mailbox_);

    while (running_)
    {
        Message msg;
        while (mailbox_->try_pop(msg))
        {
            if (msg.payload) msg.payload();
        }

        tick();
    }

    MessageBus::Get().unsubscribe(ThreadName::GameLogic);
}

void GameLogicThread::tick()
{
    double currentTime = TimeUtil::uptimeSeconds();
    dt_ = currentTime - prevTime_;
    prevTime_ = currentTime;
    if (dt_ > 0.25) dt_ = 0.25;

    tickAccumulator_ += dt_;
    double tickStart = TimeUtil::uptimeSeconds();
    while (tickAccumulator_ >= TICK_INTERVAL)
    {
        if (RenderThread::getInstance().isRunning() && RenderThread::getInstance().getScreenManager().hasScreen())
        {
            MessageBus::Get().send(ThreadName::Renderer, []() {
                RenderThread::getInstance().getScreenManager().tick(TICK_INTERVAL);
            });
        }
        tickAccumulator_ -= TICK_INTERVAL;
    }

    cpuTickMs_ = (TimeUtil::uptimeSeconds() - tickStart) * 1000.0;
    tickReady_.store(true, std::memory_order_release);
}

void GameLogicThread::stop()
{
    running_.store(false, std::memory_order_release);
    if (mailbox_)
        mailbox_->stop();
}

bool GameLogicThread::isTickReady() const
{
    return tickReady_.load(std::memory_order_acquire);
}

void GameLogicThread::ackTick()
{
    tickReady_.store(false, std::memory_order_relaxed);
}

double GameLogicThread::getCpuTickMs() const { return cpuTickMs_; }
double GameLogicThread::getDt() const { return dt_; }

}
