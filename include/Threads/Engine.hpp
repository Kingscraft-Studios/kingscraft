#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

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

        void Init();
        void Shutdown();
        static Engine& Get() {
            static Engine instance;
            return instance;
        }

        void run();

        // Called by RenderThread
        void signalStop() {
            running_ = false;
            mailbox_->stop();
            runCV_.notify_all();
        }

        FrameExchange& getFrameExchange() { return exchange; }
        MetricsExchange& getMetricsExchange() { return metricsExchange; }
        Observability& getDiagnostics() { return diagnostics; }
        CacheManager& getCacheManager() { return cacheManager; }

        void gameLogicStopped();

    private:
        std::atomic<bool> running_{true};
        std::atomic<bool> runningMailbox{true};
        std::shared_ptr<Mailbox> mailbox_;

        std::thread mailboxThread;
        std::condition_variable runCV_;
        std::mutex runMtx_;

        FrameExchange exchange;
        MetricsExchange metricsExchange;
        Observability diagnostics;
        CacheManager cacheManager;
    };

} // namespace kc
