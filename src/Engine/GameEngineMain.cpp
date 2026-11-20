#include "GameEngineMain.hpp"
#include "Bus/MessageBus.hpp"
#include "Core/WorkerPool.hpp"
#include "Threads/Engine.hpp"
#include "Threads/IO.hpp"
#include "Threads/Logger.hpp"
#include "Util/LogUtils.hpp"

namespace kc {
    int GameEngineMain::initialize(const std::string& ipcPath) {
        TimeUtil::Init();

        MessageBus::Init();

        // Starts Logger
        Logger::Get();
        IO::Init();

        LogUtils::info(ThreadName::Engine, "Core", "Kingscraft Engine starting");

        WorkerPool::get().start();

        Engine::Get().Init();
        Engine::Get().run();
        Engine::Get().Shutdown();

        WorkerPool::get().stop();

        IO::Shutdown();
        // Logger Stops at Last so before Stopping we one last time Process all Messages Left in Engine Threads if Any
        Message msg;
        while (Engine::Get().mailbox_->try_pop(msg)) {
            if (msg.payload)
                msg.payload();
        }
        LogUtils::info(ThreadName::Engine, "Core", "Kingscraft Engine stopped");
        MessageBus::Shutdown();
        return 0;
    }
}
