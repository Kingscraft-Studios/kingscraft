#include "Threads/Engine.hpp"
#include "Bus/MessageBus.hpp"
#include "Core/Bootstrapper.hpp"
#include "Event/detail/CurrentThread.hpp"

#include <chrono>
#include <exception>
#include <string>

#include "Core/Registries.hpp"
#include "Core/Runtime.hpp"
#include "Core/WorkerPool.hpp"
#include "Threads/InputThread.hpp"
#include "Util/LogUtils.hpp"

namespace kc {

    void Engine::Init(link::Connection& connection) {
        connection_ = &connection;

        // Handshake before the heavy setup starts; the Watchdog is already
        // dispatching messages and the Connection buffers any early reply.
        connection_->send(link::Message::fromText("Ping"));

        // Engine Specific Init
        mailbox_ = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::Engine, mailbox_);
        mailboxThread = std::thread([this]() {
            detail::setCurrentThread(ThreadName::Engine);
            while (runningMailbox) {
                Message msg;
                while (mailbox_->pop_for(msg, std::chrono::milliseconds(100))) {
                    if (msg.payload) {
                        msg.payload();
                    }
                }
            }
        });

        Bootstrapper::Init();
        Bootstrapper::Get().load();

        // Init Runtime Members
        Runtime::get().inputThread = WorkerPool::get().startPersistentWorker<InputThread>(Workers::Input);

        // Workers
        WorkerPool::get().startWorker([]{ Registries::build(); });

        Runtime::get().renderThread = WorkerPool::get().startPersistentWorker<RenderThread>(Workers::Renderer);
        Runtime::get().renderThread->setQuitCallback([this]() { signalStop(); });

        // Start GameLogic
        Runtime::get().kingscraft = WorkerPool::get().startPersistentWorker<Kingscraft>(Workers::Kingscraft);

        diagnostics.start();

        LogUtils::info(ThreadName::Engine, "Core", "Engine initialized");
    }

    void Engine::handleMessage(const link::Message& message) {
        const std::string response = message.asText();
        if (response == "Pong") {
            LogUtils::info(ThreadName::Engine, "Core", "Received Pong from Watchdog.");
        } else {
            LogUtils::warn(ThreadName::Engine, "Core", "Unexpected KCLink message: " + response);
        }
    }

    void Engine::handleDisconnect(link::DisconnectReason reason) {
        if (reason == link::DisconnectReason::Graceful) {
            LogUtils::info(ThreadName::Engine, "Core", "Watchdog closed the KCLink connection.");
        } else {
            LogUtils::error(ThreadName::Engine, "Core", "Lost KCLink connection to Watchdog.");
        }
        signalStop();
    }

    void Engine::gameLogicStopped() {
        if (Runtime::get().renderThread)
            Runtime::get().renderThread->requestTeardown();
    }

    void Engine::Shutdown() {
        LogUtils::info(ThreadName::Engine, "Core", "Engine shutting down");

        WorkerPool::get().stopPersistentWorker(Workers::Kingscraft);
        WorkerPool::get().stopPersistentWorker(Workers::Renderer);
        WorkerPool::get().stopPersistentWorker(Workers::Input);

        diagnostics.cleanup();

        Bootstrapper::Shutdown();

        // Engine Specific Shutdown
        runningMailbox = false;
        if (mailboxThread.joinable()) mailboxThread.join();
    }

    void Engine::run() {
        detail::setCurrentThread(ThreadName::Engine);

        if (!running_.load()) return;

        if (connection_) {
            try {
                loop_.watch(*connection_,
                    [this](link::Connection&, const link::Message& message) {
                        handleMessage(message);
                    },
                    [this](link::Connection&, link::DisconnectReason reason) {
                        handleDisconnect(reason);
                    });
                loop_.run();
            } catch (const std::exception& error) {
                LogUtils::error(ThreadName::Engine, "Core",
                                "KCLink failure: " + std::string(error.what()));
                handleDisconnect(link::DisconnectReason::PeerLost);
            }
            return;
        }

        // Fallback for runs without a KCLink connection: wait for shutdown.
        std::unique_lock lock(runMtx_);
        runCV_.wait(lock, [this]() { return !running_.load(); });
    }

} // namespace kc
