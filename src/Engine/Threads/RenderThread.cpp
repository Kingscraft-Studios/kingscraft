#include "Threads/RenderThread.hpp"
#include "Bus/MessageBus.hpp"
#include "Core/Runtime.hpp"

namespace kc {
    void RenderThread::start() {
        mailbox_ = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::Renderer, mailbox_);
    }

    void RenderThread::run() {

        while (!app->windowShouldClose()) {
            Message msg;
            while (mailbox_->try_pop(msg)) {
                if (msg.payload) msg.payload();
            }
            app->tick();
        }

        {
            std::unique_lock lock(teardownMtx);
            teardownCv.wait(lock, [this] { return teardownRequested.load(); });
        }

        stop();
    }

    void RenderThread::stop() {
        MessageBus::Get().unsubscribe(ThreadName::Renderer);
        app->cleanup();
        if (quitCallback_) quitCallback_();
        app.reset();
    }

    void RenderThread::signalQuit() {
        mailbox_->stop();
        MessageBus::Get().send(ThreadName::Input, []() {
            Runtime::get().inputThread->setWindowClose();
        });
    }

    void RenderThread::requestTeardown() {
        {
            std::lock_guard lock(teardownMtx);
            teardownRequested.store(true);
        }

        teardownCv.notify_one();
    }
} // namespace kc
