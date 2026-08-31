#include "Threads/InputThread.hpp"

#include "Bus/MessageBus.hpp"
#include "Util/LogUtils.hpp"

#include <chrono>
#include <thread>

namespace kc {
    void InputThread::Init(WindowCreateInfo info) {
        window.emplace(info.width, info.height, info.name);

        auto ext = window->getExtent();
        cachedWidth_.store(ext.width, std::memory_order_release);
        cachedHeight_.store(ext.height, std::memory_order_release);
        lastMouseX_.store(window->getLastX(), std::memory_order_release);
        lastMouseY_.store(window->getLastY(), std::memory_order_release);

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
            bool idle = true;
            while (mailbox->try_pop(msg)) {
                idle = false;
                if (msg.payload) {
                    try {
                        msg.payload();
                    } catch (std::runtime_error& e) {
                        LogUtils::error(ThreadName::Input, StringBuilder::build("Input Thread Exception: ", e.what()));
                    }
                }
            }
            window->pollGLFWEvents();
            closeRequested_.store(window->shouldClose(), std::memory_order_release);
            keyHandler->update();
            if (idle) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void InputThread::Shutdown() {
        running = false;
        initialized.store(false, std::memory_order_release);
        mailbox->stop();
        window.reset();
    }
}
