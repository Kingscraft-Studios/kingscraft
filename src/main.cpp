#include "Bus/MessageBus.hpp"
#include "Core/WorkerPool.hpp"
#include "Threads/Engine.hpp"
#include "Threads/IO.hpp"
#include "Threads/Logger.hpp"

int main() {
    kc::MessageBus::Init();

    kc::TimeUtil::Init();
    kc::IO::Init();
    kc::Logger::Init();

    kc::WorkerPool::get().start();

    kc::Engine::Get().Init();
    kc::Engine::Get().run();
    kc::Engine::Get().Shutdown();

    kc::WorkerPool::get().stop();

    kc::Logger::Shutdown();
    kc::IO::Shutdown();

    kc::MessageBus::Shutdown();
    return 0;
}
