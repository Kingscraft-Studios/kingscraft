#include "Threads/GameLogicThread.hpp"

#include "Bus/MessageBus.hpp"
#include "Core/Keys.hpp"
#include "Core/World/WorldScreen.hpp"
#include "Threads/Engine.hpp"
#include "Vulkan/App.hpp"
#include "Util/TimeUtil.hpp"
#include "Util/LogUtils.hpp"

namespace lve {
    void GameLogicThread::init() {
        prevTime_ = TimeUtil::uptimeSeconds();
        mailbox_ = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::GameLogic, mailbox_);
        world = std::make_unique<World>(terrainGen, RendererSettings::get().chunkSize, RendererSettings::get().worldHeight);
        registerAllKeys();
    }

    void GameLogicThread::run() {


        registerUICallbacks();
    while (running_) {
        Message msg;
        while (mailbox_->try_pop(msg))
        {
            if (msg.payload) msg.payload();
        }

        tick();
    }

    screenManager.reset();
    }

void GameLogicThread::tick() {
    double currentTime = TimeUtil::uptimeSeconds();
    dt_ = currentTime - prevTime_;
    prevTime_ = currentTime;
    if (dt_ > 0.25) dt_ = 0.25;

    tickAccumulator_ += dt_;
    double tickStart = TimeUtil::uptimeSeconds();
    while (tickAccumulator_ >= TICK_INTERVAL)
    {
        if (screenManager->hasScreen()) {
            screenManager->tick(TICK_INTERVAL);
        }
        tickAccumulator_ -= TICK_INTERVAL;
    }

    cpuTickMs_ = (TimeUtil::uptimeSeconds() - tickStart) * 1000.0;
        FrameExchange& exchange = Engine::Get().getFrameExchange();
        FrameScene& scene = exchange.writeFrame();
        if (screenManager->hasScreen()) {
            screenManager->render(scene);
        }
        scene.stats.delta = dt_;
        scene.stats.cpuTickMs = cpuTickMs_;
        exchange.publish();

    tickReady_.store(true, std::memory_order_release);
}

void GameLogicThread::stop() {
    running_.store(false, std::memory_order_release);
    if (mailbox_) {
        mailbox_->stop();
    }
        MessageBus::Get().unsubscribe(ThreadName::GameLogic);
}

void GameLogicThread::registerAllKeys() {

        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().getKeyBindHandler().onPress(BindLayer::Global, {Keys::F11}, []() {
                MessageBus::Get().send(ThreadName::Input, []() {
                    InputThread::getInstance().toggleFullscreen();
                });
                MessageBus::Get().send(ThreadName::Renderer, []() {
                    RenderThread::getInstance().recreateSwapchain(true);
                });
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().getKeyBindHandler().onPress(BindLayer::Global, {Keys::ESCAPE}, []() {
                MessageBus::Get().send(ThreadName::Input, []() {
                    InputThread::getInstance().setWindowClose();
                });
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().getKeyBindHandler().onPress(BindLayer::Global, {Keys::F3, Keys::F6}, []() {
                RenderThread::getInstance().getUI().setDebugMode(!RenderThread::getInstance().getUI().isDebugModeOn());
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().getKeyBindHandler().onPress(BindLayer::Global, {Keys::F7}, []() {
                if (RenderThread::getInstance().getUI().isDebugModeOn())
                    RenderThread::getInstance().getUI().logSelectedElementPosition();
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().getKeyBindHandler().onPress(BindLayer::Global, {Keys::F8}, []() {
                if (!RenderThread::getInstance().getProfilerCapture().isActive()) {
                    RenderThread::getInstance().getProfilerCapture().start();
                }
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().setMouseMoveCallback([](double x, double y) {
                MessageBus::Get().send(ThreadName::Renderer, [x, y]() {
                    RenderThread::getInstance().getUI().onMouseMove(x, y);
                });
            });
        });
        MessageBus::Get().send(ThreadName::Input, [this]() {
            InputThread::getInstance().setMouseButtonCallback([this](int button, int action, int mods) {
                double x = InputThread::getInstance().getLastX();
                double y = InputThread::getInstance().getLastY();
                MessageBus::Get().send(ThreadName::Renderer, [this, button, action, mods, x, y]() {
                    RenderThread::getInstance().getUI().onMouseButton(button, action, mods, x, y);
                    if (screenManager->getCurrent())
                        screenManager->getCurrent()->onMouseButton(button, action, mods);
                });
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().setScrollCallback([](double dx, double dy) {
                MessageBus::Get().send(ThreadName::Renderer, [dx, dy]() {
                    RenderThread::getInstance().getUI().onScroll(dx, dy);
                });
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().setKeyCallback([](int key, int scancode, int action, int mods) {
                InputThread::getInstance().getKeyBindHandler().onKeyEvent(key, scancode, action, mods);
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().setCharCallback([](unsigned int codepoint) {
                InputThread::getInstance().getKeyBindHandler().onChar(codepoint);
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().getKeyBindHandler().setUiKeyCallback([](int key, int action) {
                RenderThread::getInstance().getUI().onKey(key, action);
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().getKeyBindHandler().setUiCharCallback([](unsigned int codepoint) {
                RenderThread::getInstance().getUI().onChar(codepoint);
            });
        });
}

    void GameLogicThread::registerUICallbacks() {
        RenderThread::getInstance().getUI().registerButtonHandler(BTN_QUIT_GAME, []() {
                MessageBus::Get().send(ThreadName::Input, []() {
                    InputThread::getInstance().setWindowClose();
                });
            });

        RenderThread::getInstance().getUI().registerButtonHandler(BTN_ENTER_WORLD, [this]() {
            MessageBus::Get().send(ThreadName::GameLogic, [this]() {
                screenManager->setScreen<WorldScreen>();
            });
        });
}

bool GameLogicThread::isTickReady() const {
    return tickReady_.load(std::memory_order_acquire);
}

void GameLogicThread::ackTick() {
    tickReady_.store(false, std::memory_order_relaxed);
}

double GameLogicThread::getCpuTickMs() const { return cpuTickMs_; }
double GameLogicThread::getDelta() const { return dt_; }

}
