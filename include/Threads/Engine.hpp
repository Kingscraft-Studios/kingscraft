#pragma once

#include <memory>
#include <thread>
#include <atomic>

#include "Bus/Mailbox.hpp"
#include "Core/Diagnostics/Diagnostics.hpp"
#include "Renderer/FrameExchange.hpp"
#include "Threads/Renderer.hpp"
#include "Threads/ResourceLoader.hpp"
#include "Threads/GameLogicThread.hpp"

namespace lve {

    class Engine {
    public:
        Engine() = default;
        ~Engine();

        static void Init();
        static void Shutdown();
        static Engine& Get();
        static Engine& get() { return Get(); }

    void run();
    void stop();

    Mailbox& getMailbox() { return *mailbox_; }
    FrameExchange& getFrameExchange() { return exchange; }
    Diagnostics& getDiagnostics() { return diagnostics; }

private:
    std::atomic<bool> running_{true};
    std::shared_ptr<Mailbox> mailbox_;
    std::thread regThread_;
    std::thread rendererThread_;
    std::thread resLoaderThread_;
    std::thread inputThread;
    ResourceLoader resourceLoader_;
    std::thread gameLogicThread_;
    std::atomic<bool> shutdownComplete_{false};

    FrameExchange exchange;
    Diagnostics diagnostics;

    static std::unique_ptr<Engine> instance_;
};

} // namespace lve
