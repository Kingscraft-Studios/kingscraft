#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <KCLink/Connection.hpp>
#include <KCLink/EventLoop.hpp>

#include "Bus/Mailbox.hpp"
#include "Cache/CacheManger.hpp"
#include "Core/Observability/Observability.hpp"
#include "Core/Observability/MetricsExchange.hpp"
#include "Renderer/FrameExchange.hpp"
#include "Threads/RenderThread.hpp"
#include "Threads/Kingscraft.hpp"

namespace kc {

    class Engine {
    public:
        Engine() = default;

        // The connection is owned by GameEngineMain; Engine keeps a non-owning
        // reference to it and sends the KCLink handshake during Init.
        void Init(link::Connection& connection);
        void Shutdown();
        static Engine& Get() {
            static Engine instance;
            return instance;
        }

        void run();

        // Called by RenderThread
        void signalStop() {
            running_ = false;
            runCV_.notify_all();
            loop_.quit();
        }

        FrameExchange& getFrameExchange() { return exchange; }
        MetricsExchange& getMetricsExchange() { return metricsExchange; }
        Observability& getDiagnostics() { return diagnostics; }
        CacheManager& getCacheManager() { return cacheManager; }

        void gameLogicStopped();

        std::shared_ptr<Mailbox> mailbox_;

    private:
        void handleMessage(const link::Message& message);
        void handleDisconnect(link::DisconnectReason reason);

        std::atomic<bool> running_{true};
        std::atomic<bool> runningMailbox{true};

        std::thread mailboxThread;
        std::condition_variable runCV_;
        std::mutex runMtx_;

        link::Connection* connection_ = nullptr;
        link::EventLoop loop_;

        FrameExchange exchange;
        MetricsExchange metricsExchange;
        Observability diagnostics;
        CacheManager cacheManager;
    };

} // namespace kc
