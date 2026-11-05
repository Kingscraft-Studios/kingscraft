#include "Threads/InputThread.hpp"

#include "Bus/MessageBus.hpp"
#include "Core/Runtime.hpp"
#include "Threads/Kingscraft.hpp"
#include "Threads/RenderThread.hpp"

#include <cassert>
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

        // Key/char events feed the InputSystem directly on this thread. No
        // game-object callbacks are stored here: the system only holds POD
        // action IDs and posts ActionEvent values to GameLogic.
        window->setKeyCallback([this](int key, int scancode, int action, int mods) {
            inputSystem_->onKey(key, scancode, action, mods);
        });
        window->setCharCallback([this](unsigned int codepoint) {
            inputSystem_->onChar(codepoint);
        });

        // Mouse/scroll callbacks are installed here, on the Input thread, and
        // capture only POD values. The destinations are resolved on the target
        // thread through Runtime, so no game object is ever held by the input
        // thread.
        window->setMouseMoveCallback([](double x, double y) {
            MessageBus::Get().send(ThreadName::Renderer, [x, y]() {
                if (auto* renderer = Runtime::get().renderThread)
                    renderer->getUI().onMouseMove(x, y);
            });
        });
        window->setMouseButtonCallback([](int button, int action, int mods) {
            const double x = Runtime::get().inputThread->getLastX();
            const double y = Runtime::get().inputThread->getLastY();

            MessageBus::Get().send(ThreadName::Renderer, [button, action, mods, x, y]() {
                if (auto* renderer = Runtime::get().renderThread)
                    renderer->getUI().onMouseButton(button, action, mods, x, y);
            });
            MessageBus::Get().send(ThreadName::GameLogic, [button, action, mods]() {
                auto* kingscraft = Runtime::get().kingscraft;
                if (!kingscraft) return;
                if (auto* screen = kingscraft->getScreenManager().getCurrent())
                    screen->onMouseButton(button, action, mods);
            });
        });
        window->setScrollCallback([](double dx, double dy) {
            MessageBus::Get().send(ThreadName::Renderer, [dx, dy]() {
                if (auto* renderer = Runtime::get().renderThread)
                    renderer->getUI().onScroll(dx, dy);
            });
            MessageBus::Get().send(ThreadName::GameLogic, [dx, dy]() {
                auto* kingscraft = Runtime::get().kingscraft;
                if (!kingscraft) return;
                if (auto* screen = kingscraft->getScreenManager().getCurrent())
                    screen->onScroll(dx, dy);
            });
        });

        mailbox = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::Input, mailbox);

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
            inputSystem_->update();

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
        inputSystem_.reset();
        registry_.reset();
    }
}
