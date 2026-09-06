#include "Threads/InputThread.hpp"

#include "Bus/MessageBus.hpp"

#include <chrono>
#include <thread>

namespace kc {
    void InputThread::start() {

        window->setResizeHook([this](int w, int h) {
            cachedWidth_.store(static_cast<uint32_t>(w), std::memory_order_release);
            cachedHeight_.store(static_cast<uint32_t>(h), std::memory_order_release);
            windowResized_.store(true, std::memory_order_release);
        });
        window->setMousePosHook([this](double x, double y) {
            lastMouseX_.store(x, std::memory_order_release);
            lastMouseY_.store(y, std::memory_order_release);
        });

        mailbox = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::Input, mailbox);
        keyHandler->setDispatcher([](std::function<void()> cb) {
            MessageBus::Get().send(ThreadName::GameLogic, [cb = std::move(cb)]() {
                cb();  // executes on game logic thread
            });
        });
        running = true;
        initialized.store(true, std::memory_order_release);
    }

    void InputThread::run() {
        while (running) {
            Message msg;
            while (mailbox->try_pop(msg)) {
                if (msg.payload) {
                    msg.payload();
                }
            }
            window->pollGLFWEvents();
            closeRequested_.store(window->shouldClose(), std::memory_order_release);
            keyHandler->update();

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        stop();

    }

    void InputThread::signalQuit() {
        running = false;
        initialized.store(false, std::memory_order_release);
        mailbox->stop();
    }

    void InputThread::stop() {
        window.reset();
        keyHandler.reset();
    }
}
