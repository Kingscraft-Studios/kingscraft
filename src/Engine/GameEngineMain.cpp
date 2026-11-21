#include "GameEngineMain.hpp"
#include <KCLink/KCLink.hpp>

#include "Bus/MessageBus.hpp"
#include "Core/WorkerPool.hpp"
#include "Threads/Engine.hpp"
#include "Threads/IO.hpp"
#include "Threads/Logger.hpp"
#include "Util/LogUtils.hpp"

#include <exception>
#include <stdexcept>
#include <string>

namespace kc {
    int GameEngineMain::initialize(const std::string& ipcPath) {
        link::Connection connection;
        try {
            link::Client client;
            connection = client.connect(ipcPath);
        } catch (const std::exception& error) {
            throw std::runtime_error(
                std::string("[Game Engine] KCLink connect failed: ") + error.what());
        }

        TimeUtil::Init();

        MessageBus::Init();

        // Starts Logger
        Logger::Get();
        IO::Init();

        LogUtils::info(ThreadName::Engine, "Core", "Kingscraft Engine starting");

        WorkerPool::get().start();

        Engine::Get().Init(connection);
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

        connection.disconnect();
        return 0;
    }
}
