#include "Threads/Renderer.hpp"
#include "Bus/MessageBus.hpp"

namespace lve {

    void RenderThread::run() {
        mailbox_ = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::Renderer, mailbox_);

        while (!app->windowShouldClose()) {
            Message msg;
            while (mailbox_->try_pop(msg)) {
                if (msg.payload) msg.payload();
            }
            app->tick();
        }
        app->cleanup();

        MessageBus::Get().unsubscribe(ThreadName::Renderer);
        if (quitCallback_) quitCallback_();
    }

    void RenderThread::shutdown() {
        mailbox_->stop();
        app.reset();
    }
} // namespace lve
