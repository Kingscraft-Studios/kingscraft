#include "Threads/Engine.hpp"
#include "Threads/Logger.hpp"
#include "Bus/MessageBus.hpp"
#include "Core/Bootstrapper.hpp"

#include <chrono>

#include "Core/Registries.hpp"
#include "Core/Runtime.hpp"
#include "Core/WorkerPool.hpp"
#include "Threads/InputThread.hpp"
#include "Util/LogUtils.hpp"

namespace kc {

    void Engine::Init() {
        // Engine Specific Init
        mailbox_ = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::Engine, mailbox_);
        mailboxThread = std::thread([this]() {
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
    }

    void Engine::gameLogicStopped() {
        if (Runtime::get().renderThread)
            Runtime::get().renderThread->requestTeardown();
    }

    void Engine::Shutdown() {
        LogUtils::info(ThreadName::Engine, "Shutting down");

        WorkerPool::get().stopPersistentWorker(Workers::Kingscraft);
        WorkerPool::get().stopPersistentWorker(Workers::Renderer);
        WorkerPool::get().stopPersistentWorker(Workers::Input);

        diagnostics.cleanup();

        // Engine Specific Shutdown
        MessageBus::Get().unsubscribe(ThreadName::Engine);
        runningMailbox = false;
        mailbox_->stop();
        if (mailboxThread.joinable()) mailboxThread.join();
    }

    void Engine::run() {
        while (running_) {
            {
                std::unique_lock lock(runMtx_);
                runCV_.wait_for(lock, std::chrono::duration<double>(diagnostics.nextDispatchIn()),
                                [this]() { return !running_.load(); });
            }
            if (!running_) break;

            diagnostics.update();
            diagnostics.send();
        }
    }

} // namespace kc
