#include "GameEngineMain.hpp"
#include <KCLink/KCLink.hpp>

#include "Bus/MessageBus.hpp"
#include "Core/WorkerPool.hpp"
#include "Threads/Engine.hpp"
#include "Threads/IO.hpp"
#include "Threads/Logger.hpp"
#include "Util/LogUtils.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace kc {
    int GameEngineMain::initialize(const std::string& ipcPath) {
        link::Connection connection;
        try {
            link::Client client;
            connection = client.connect(ipcPath);

            const std::string ping = "Ping";
            connection.send(link::Message(std::vector<std::uint8_t>(ping.begin(), ping.end())));

            connection.setReceiveTimeout(std::chrono::seconds(10));
            link::Message incoming;
            if (!connection.receive(incoming)) {
                std::cerr << "[Game Engine] Watchdog disconnected before replying.\n";
                return 1;
            }

            const std::string response(incoming.payload().begin(), incoming.payload().end());
            if (response != "Pong") {
                std::cerr << "[Game Engine] Unexpected response from Watchdog: " << response << std::endl;
                return 1;
            }
            std::cout << "[Game Engine] Received Pong from Watchdog." << std::endl;
        } catch (const std::exception& error) {
            std::cerr << "[Game Engine] KCLink handshake failed: " << error.what() << std::endl;
            return 1;
        }

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
        connection.close();
        return 0;
    }
}
