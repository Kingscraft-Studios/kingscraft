#include "Threads/Kingscraft.hpp"

#include "Bus/MessageBus.hpp"
#include "Core/Keys.hpp"
#include "Core/World/WorldScreen.hpp"
#include "Threads/Engine.hpp"
#include "Threads/IO.hpp"
#include "Util/TimeUtil.hpp"
#include "Util/LogUtils.hpp"

#include <chrono>
#include <cmath>
#include <thread>

#include "Core/MainMenu.hpp"
#include "Core/Registries.hpp"
#include "Core/Runtime.hpp"

namespace kc {
    void Kingscraft::start() {
        mailbox_ = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::GameLogic, mailbox_);

        MessageBus::Get().request<DecodedTextureData>(ThreadName::Engine,[] {
            return IO::Get().getBuiltinTemplates().getTextureTemplate().loadDecoded("resources/textures/logo/Kingscraft-Logo.png");
        }, ThreadName::Input, [](DecodedTextureData tex) {
            if (tex.isValid())
                Runtime::get().inputThread->setIcon(tex.pixels.data(), tex.width, tex.height);
        });

        prevTime_ = TimeUtil::uptimeSeconds();

        // The InputSystem owns this registry; the router is the GameLogic-side
        // handle used to add contexts and subscribe to actions.
        actions_.setRegistry(Runtime::get().inputThread->getInputSystem().getRegistry());

        registerAllKeys();
        registerUICallbacks();
        screenManager->setScreen<MainMenu>(actions_);
    }

    void Kingscraft::run() {

        while (!Runtime::get().inputThread->shouldClose()) {
            Message msg;
            while (mailbox_->try_pop(msg)) {
                if (msg.payload) msg.payload();
            }

            tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        stop();
    }

    void Kingscraft::signalQuit() {
        if (mailbox_) {
            mailbox_->stop();
        }
    }

    void Kingscraft::stop() {
        // Tear screens/world down while the router and registry are still alive
        // so every RAII context/subscription can unregister cleanly.
        screenManager.reset();
        worldManager.destroyWorld();

        globalContext_.reset();
        fullscreenSub_.reset();
        debugSub_.reset();
        logElementSub_.reset();
        profilerSub_.reset();
        reloadSub_.reset();
        dumpSub_.reset();
        actions_.setRegistry(nullptr);

        MessageBus::Get().unsubscribe(ThreadName::GameLogic);
        Engine::Get().gameLogicStopped();
    }


    void Kingscraft::tick() {
        double currentTime = TimeUtil::uptimeSeconds();
        dt_ = currentTime - prevTime_;
        prevTime_ = currentTime;
        if (dt_ > 0.25) dt_ = 0.25;

        if (World* world = worldManager.getWorld()) {
            const glm::vec3 body = world->getPlayerController().getBodyPosition();

            // The terrain generator is owned by WorldManager.
            // We will expose a getter for it in the next step.
            currentBiomeEncoded_.store(worldManager.getTerrainGenerator()->getBiomeAt(static_cast<int>(std::floor(body.x)),
                static_cast<int>(std::floor(body.z))).getEncoded(), std::memory_order_relaxed);

            playerFlying_.store(world->getPlayerController().isFlying(), std::memory_order_relaxed);
        } else {
            currentBiomeEncoded_.store(0, std::memory_order_relaxed);
            playerFlying_.store(false, std::memory_order_relaxed);
        }

        tickAccumulator_ += dt_;
        double tickStart = TimeUtil::uptimeSeconds();
        while (tickAccumulator_ >= TICK_INTERVAL) {
            // Tick Within 100hz Timer
            if (screenManager->hasScreen()) {
                screenManager->tick(TICK_INTERVAL);
            }
            tickAccumulator_ -= TICK_INTERVAL;
        }

        cpuTickMs_ = (TimeUtil::uptimeSeconds() - tickStart) * 1000.0;

        // Publish diagnostics from the LAST tick's CPU metrics (cpu_ still holds
        // them) plus the renderer's latest GPU sample, before this tick mutates
        // cpu_. Fires MetricsUpdateEvent for the listeners (profiler fed every
        // tick, overlay self-throttles).
        MetricsExchange& metricsExchange = Engine::Get().getMetricsExchange();
        const RenderThreadMetrics& gpu = metricsExchange.readMetrics();
        Engine::Get().getDiagnostics().update(getCPUMetrics(), gpu);
        metricsExchange.endRead();

        FrameExchange& exchange = Engine::Get().getFrameExchange();
        FrameScene* scene = exchange.writeFrame();
        if (!scene) {
            return;
        }
        if (screenManager->hasScreen()) {
            screenManager->render(*scene);
        }
        // Set Statistics
        scene->stats.delta = dt_;
        scene->stats.cpuTickMs = cpuTickMs_;

        GameLogicMetrics& cpuMetrics = getCPUMetrics();
        cpuMetrics.delta = dt_;
        cpuMetrics.tickMs = cpuTickMs_;
        exchange.publish();
    }

    // All handlers below run on the GameLogic thread. Anything that touches
    // InputThread or RenderThread is marshalled through MessageBus; the router
    // never auto-marshals, because one action often needs work on several
    // threads (F11 toggles the window and recreates the swapchain).
    void Kingscraft::registerAllKeys() {
        globalContext_ = actions_.addContext(ContextSpec{
            .name = "global",
            .priority = 0,
            .captureText = false,
            .bindings = {
                {{Keys::F11}, Action::ToggleFullscreen},
                {{Keys::F3, Keys::F6}, Action::ToggleDebug},
                {{Keys::F7}, Action::LogSelectedElement},
                {{Keys::F8}, Action::StartProfiler},
                {{Keys::F10}, Action::ReloadRegistries},
                {{Keys::F5}, Action::DumpRegions},
            },
        });

        fullscreenSub_ = actions_.subscribe(Action::ToggleFullscreen, [](const ActionEvent& event) {
            if (event.phase != Phase::Pressed) return;

            MessageBus::Get().send(ThreadName::Input, []() {
                if (auto* input = Runtime::get().inputThread)
                    input->toggleFullscreen();
            });
            MessageBus::Get().send(ThreadName::Renderer, []() {
                if (auto* renderer = Runtime::get().renderThread)
                    renderer->recreateSwapchain(true);
            });
        });

        debugSub_ = actions_.subscribe(Action::ToggleDebug, [](const ActionEvent& event) {
            if (event.phase != Phase::Pressed) return;

            MessageBus::Get().send(ThreadName::Renderer, []() {
                if (auto* renderer = Runtime::get().renderThread) {
                    auto& ui = renderer->getUI();
                    ui.setDebugMode(!ui.isDebugModeOn());
                }
            });
        });

        logElementSub_ = actions_.subscribe(Action::LogSelectedElement, [](const ActionEvent& event) {
            if (event.phase != Phase::Pressed) return;

            MessageBus::Get().send(ThreadName::Renderer, []() {
                if (auto* renderer = Runtime::get().renderThread) {
                    auto& ui = renderer->getUI();
                    if (ui.isDebugModeOn())
                        ui.logSelectedElementPosition();
                }
            });
        });

        profilerSub_ = actions_.subscribe(Action::StartProfiler, [](const ActionEvent& event) {
            if (event.phase != Phase::Pressed) return;

            // ProfilingCapture is fed by ProfilingCaptureListener on GameLogic
            // (MetricsUpdateEvent is synchronous), so start() must stay on this
            // thread or it would race feedFrame().
            auto* renderer = Runtime::get().renderThread;
            if (!renderer) return;

            auto& capture = renderer->getProfilerCapture();
            if (!capture.isActive())
                capture.start();
        });

        reloadSub_ = actions_.subscribe(Action::ReloadRegistries, [](const ActionEvent& event) {
            if (event.phase != Phase::Pressed) return;
            Registries::reload();
        });

        dumpSub_ = actions_.subscribe(Action::DumpRegions, [](const ActionEvent& event) {
            if (event.phase != Phase::Pressed) return;

            // Region-dump debug switch. First press records every chunk: those
            // already loaded are staged right away, and the rest are staged as
            // they unload. A second press stops recording and flushes the
            // staged regions to disk.
            auto* world = Runtime::get().kingscraft->getWorldManager().getWorld();
            if (!world) return;

            if (world->getSaveAllChunks()) {
                world->dumpLoadedChunks();
                world->setSaveAllChunks(false);
                if (!IO::Get().getBuiltinTemplates().getRegionTemplate().flush())
                    LogUtils::error(ThreadName::GameLogic, "SaveAllChunks: flush failed");
            } else {
                world->setSaveAllChunks(true);
                world->dumpLoadedChunks();
                LogUtils::info(ThreadName::GameLogic, "SaveAllChunks: ON (recording loaded + unloaded chunks)");
            }
        });
    }

    void Kingscraft::registerUICallbacks() {
        Runtime::get().renderThread->getUI().registerButtonHandler(BTN_QUIT_GAME, []() {
                MessageBus::Get().send(ThreadName::Input, []() {
                    if (auto* input = Runtime::get().inputThread)
                        input->setWindowClose();
                });
            });

        // UI handlers execute on the Renderer; resolve the game state through
        // Runtime on GameLogic instead of capturing this Kingscraft instance.
        Runtime::get().renderThread->getUI().registerButtonHandler(BTN_ENTER_WORLD, []() {
            MessageBus::Get().send(ThreadName::GameLogic, []() {
                auto* kingscraft = Runtime::get().kingscraft;
                if (!kingscraft) return;

                kingscraft->getWorldManager().createWorld([kingscraft](bool success) {
                    if (!success) {
                        LogUtils::error(ThreadName::GameLogic, "Failed to create World Session");
                        return;
                    }
                    kingscraft->setScreen<WorldScreen>();
                });
            });
        });

        Runtime::get().renderThread->getUI().registerButtonHandler(BTN_RESPAWN, []() {
            MessageBus::Get().send(ThreadName::GameLogic, []() {
            auto* world = Runtime::get().kingscraft->getWorldManager().getWorld();
            if (!world) return;

            world->getPlayerController().respawn();
            });
        });
    }

}
