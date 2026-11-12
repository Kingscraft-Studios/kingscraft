#include "Bus/MessageBus.hpp"
#include "Core/WorkerPool.hpp"
#include "Threads/Engine.hpp"
#include "Threads/IO.hpp"
#include "Threads/Logger.hpp"
#include "Util/LogUtils.hpp"

int main() {
    kc::TimeUtil::Init();

    kc::MessageBus::Init();

    // Starts Logger
    kc::Logger::Get();
    kc::IO::Init();

    kc::LogUtils::info(kc::ThreadName::Engine, "Core", "Kingscraft Engine starting");

    kc::WorkerPool::get().start();

    kc::Engine::Get().Init();
    kc::Engine::Get().run();
    kc::Engine::Get().Shutdown();

    kc::WorkerPool::get().stop();

    kc::IO::Shutdown();
    // Logger Stops at Last so before Stopping we one last time Process all Messages Left in Engine Threads if Any
    kc::Message msg;
    while (kc::Engine::Get().mailbox_->try_pop(msg)) {
        if (msg.payload)
            msg.payload();
    }
    kc::LogUtils::info(kc::ThreadName::Engine, "Core", "Kingscraft Engine stopped");
    kc::MessageBus::Shutdown();
    return 0;
}
