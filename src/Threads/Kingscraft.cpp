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

        // Fresh WorldMetadata defaults a brand-new world; World loads the real
        // world.kcw async on the engine (IO) thread via MessageBus::request and
        // applies it on this thread before the first tick (re-seeding terrain
        // and restoring spawn/time through metadata_.settings / player state).
        WorldMetadata fresh;
        terrainGen = std::make_unique<DefaultTerrainGenerator>(fresh.settings);

        // How far the spawn scan is allowed to travel for dry land, in blocks.
        // Wider than the render distance on purpose, so the player can start on
        // land even when the middle of the map is open ocean.
        constexpr int kNewWorldSpawnRadius = 1536;

        // A brand new world starts on dry land near the origin instead of at the
        // hardcoded (67, 67), which for many seeds sits far out at sea. This runs
        // against the terrain generator directly, so it costs no chunk generation.
        {
            const int originX = static_cast<int>(std::floor(fresh.spawnPos.x));
            const int originZ = static_cast<int>(std::floor(fresh.spawnPos.z));
            int landX = 0;
            int landY = 0;
            int landZ = 0;
            if (terrainGen->findSpawnColumn(originX, originZ, kNewWorldSpawnRadius,
                                            landX, landY, landZ)) {
                // Eye height is added back because metadata stores eye position,
                // not feet position.
                const glm::vec3 onLand(static_cast<float>(landX) + 0.5f,
                                       static_cast<float>(landY) + Attributes::EYE_HEIGHT,
                                       static_cast<float>(landZ) + 0.5f);
                fresh.spawnPos = onLand;
                fresh.playerPos = onLand;
                LogUtils::info(ThreadName::GameLogic,
                    "world: new spawn placed on land at (" + std::to_string(landX) + ", "
                    + std::to_string(landY) + ", " + std::to_string(landZ) + ")");
            } else {
                LogUtils::warn(ThreadName::GameLogic,
                    "world: no dry land within " + std::to_string(kNewWorldSpawnRadius)
                    + " blocks of (" + std::to_string(originX) + ", "
                    + std::to_string(originZ) + "); keeping the default spawn");
            }
        }

        world = std::make_unique<World>(*terrainGen, RendererSettings::get().chunkSize,
                                        RendererSettings::get().worldHeight, fresh);
        registerAllKeys();
        registerUICallbacks();
        screenManager->setScreen<MainMenu>();
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
        screenManager.reset();
        world.reset();
        MessageBus::Get().unsubscribe(ThreadName::GameLogic);
        Engine::Get().gameLogicStopped();
    }


    void Kingscraft::tick() {
        double currentTime = TimeUtil::uptimeSeconds();
        dt_ = currentTime - prevTime_;
        prevTime_ = currentTime;
        if (dt_ > 0.25) dt_ = 0.25;

        // Publish the biome under the player for the debug overlay (read-only
        // on the renderer thread via an atomic).
        if (terrainGen && world) {
            glm::vec3 body = world->getPlayerController().getBodyPosition();
            currentBiomeEncoded_.store(
                terrainGen->getBiomeProvider()
                    .getBiome(static_cast<int>(std::floor(body.x)),
                              static_cast<int>(std::floor(body.z)))
                    .getEncoded(),
                std::memory_order_relaxed);
            playerFlying_.store(world->getPlayerController().isFlying(), std::memory_order_relaxed);
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

        auto& cpuMetrics = Engine::Get().getDiagnostics().getCPUMetrics();
        cpuMetrics.delta = dt_;
        cpuMetrics.tickMs = cpuTickMs_;
        exchange.publish();
    }

    void Kingscraft::registerAllKeys() {

        MessageBus::Get().send(ThreadName::Input, []() {
            Runtime::get().inputThread->getKeyBindHandler().onPress(BindLayer::Global, {Keys::F11}, []() {
                MessageBus::Get().send(ThreadName::Input, []() {
                    Runtime::get().inputThread->toggleFullscreen();
                });
                MessageBus::Get().send(ThreadName::Renderer, []() {
                    Runtime::get().renderThread->recreateSwapchain(true);
                });
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            Runtime::get().inputThread->getKeyBindHandler().onPress(BindLayer::Global, {Keys::ESCAPE}, []() {
                MessageBus::Get().send(ThreadName::Input, []() {
                    Runtime::get().inputThread->setWindowClose();
                });
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            Runtime::get().inputThread->getKeyBindHandler().onPress(BindLayer::Global, {Keys::F3, Keys::F6}, []() {
                Runtime::get().renderThread->getUI().setDebugMode(!Runtime::get().renderThread->getUI().isDebugModeOn());
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            Runtime::get().inputThread->getKeyBindHandler().onPress(BindLayer::Global, {Keys::F7}, []() {
                if (Runtime::get().renderThread->getUI().isDebugModeOn())
                    Runtime::get().renderThread->getUI().logSelectedElementPosition();
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            Runtime::get().inputThread->getKeyBindHandler().onPress(BindLayer::Global, {Keys::F8}, []() {
                if (!Runtime::get().renderThread->getProfilerCapture().isActive()) {
                    Runtime::get().renderThread->getProfilerCapture().start();
                }
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            Runtime::get().inputThread->getKeyBindHandler().onPress(BindLayer::Global, {Keys::F10}, []() {
                Registries::reload();
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            Runtime::get().inputThread->getKeyBindHandler().onPress(BindLayer::Global, {Keys::F5}, []() {
                // Toggle the region-dump debug switch. ON starts recording every
                // loaded chunk; a second press stops recording and flushes the
                // staged regions to disk immediately.
                MessageBus::Get().send(ThreadName::GameLogic, []() {
                    auto& world = Runtime::get().kingscraft->getWorld();
                    if (world.getSaveAllChunks()) {
                        world.setSaveAllChunks(false);
                        IO::Get().getBuiltinTemplates().getRegionTemplate().flush();
                        LogUtils::info(ThreadName::GameLogic, "SaveAllChunks: OFF (region dump flushed to disk)");
                    } else {
                        world.setSaveAllChunks(true);
                        LogUtils::info(ThreadName::GameLogic, "SaveAllChunks: ON (recording every loaded chunk)");
                    }
                });
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            Runtime::get().inputThread->setMouseMoveCallback([](double x, double y) {
                MessageBus::Get().send(ThreadName::Renderer, [x, y]() {
                    Runtime::get().renderThread->getUI().onMouseMove(x, y);
                });
            });
        });
        MessageBus::Get().send(ThreadName::Input, [this]() {
            Runtime::get().inputThread->setMouseButtonCallback([this](int button, int action, int mods) {
                double x = Runtime::get().inputThread->getLastX();
                double y = Runtime::get().inputThread->getLastY();
                MessageBus::Get().send(ThreadName::Renderer, [button, action, mods, x, y]() {
                    Runtime::get().renderThread->getUI().onMouseButton(button, action, mods, x, y);
                });
                MessageBus::Get().send(ThreadName::GameLogic, [this, button, action, mods]() {
                    if (screenManager->getCurrent())
                        screenManager->getCurrent()->onMouseButton(button, action, mods);
                });
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            Runtime::get().inputThread->setScrollCallback([](double dx, double dy) {
                MessageBus::Get().send(ThreadName::Renderer, [dx, dy]() {
                    Runtime::get().renderThread->getUI().onScroll(dx, dy);
                });
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            Runtime::get().inputThread->setKeyCallback([](int key, int scancode, int action, int mods) {
                Runtime::get().inputThread->getKeyBindHandler().onKeyEvent(key, scancode, action, mods);
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            Runtime::get().inputThread->setCharCallback([](unsigned int codepoint) {
                Runtime::get().inputThread->getKeyBindHandler().onChar(codepoint);
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            Runtime::get().inputThread->getKeyBindHandler().setUiKeyCallback([](int key, int action) {
                Runtime::get().renderThread->getUI().onKey(key, action);
            });
        });
        MessageBus::Get().send(ThreadName::Input, []() {
            Runtime::get().inputThread->getKeyBindHandler().setUiCharCallback([](unsigned int codepoint) {
                Runtime::get().renderThread->getUI().onChar(codepoint);
            });
        });
    }

    void Kingscraft::registerUICallbacks() {
        Runtime::get().renderThread->getUI().registerButtonHandler(BTN_QUIT_GAME, []() {
                MessageBus::Get().send(ThreadName::Input, []() {
                    Runtime::get().inputThread->setWindowClose();
                });
            });

        Runtime::get().renderThread->getUI().registerButtonHandler(BTN_ENTER_WORLD, [this]() {
            MessageBus::Get().send(ThreadName::GameLogic, [this]() {
                screenManager->setScreen<WorldScreen>();
            });
        });

        Runtime::get().renderThread->getUI().registerButtonHandler(BTN_RESPAWN, []() {
            MessageBus::Get().send(ThreadName::GameLogic, []() {
                Runtime::get().kingscraft->getWorld().getPlayerController().respawn();
            });
        });
    }

}
