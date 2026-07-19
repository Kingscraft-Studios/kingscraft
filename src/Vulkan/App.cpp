#include "Vulkan/App.hpp"
#include "Core/MainMenu.hpp"
#include "Core/World/WorldScreen.hpp"
#include "Renderer/Bloom.hpp"
#include "Renderer/RendererSettings.hpp"
#include <chrono>
#include <thread>

#include "Bus/MessageBus.hpp"
#include "Threads/InputThread.hpp"
#include "Threads/Engine.hpp"
#include "Util/LogUtils.hpp"

namespace lve {

    App::App() {

        MessageBus::Get().send(ThreadName::Input, [this]() {
            InputThread::getInstance().getKeyBindHandler().onPress(BindLayer::Global, {Keys::F11}, [this]() {
                MessageBus::Get().send(ThreadName::Input, []() {
                    InputThread::getInstance().toggleFullscreen();
                });
                requestSwapchainRecreate = true;
            });
        });

        MessageBus::Get().send(ThreadName::Input, []() {
            InputThread::getInstance().getKeyBindHandler().onPress(BindLayer::Global, {Keys::ESCAPE}, []() {
                MessageBus::Get().send(ThreadName::Input, []() {
                    InputThread::getInstance().setWindowClose();
                });
            });
        });



        MessageBus::Get().send(ThreadName::Input, [this]() {
            InputThread::getInstance().getKeyBindHandler().onPress(BindLayer::Global, {Keys::F3, Keys::F6}, [this]() {
                uiSystem->setDebugMode(!uiSystem->isDebugModeOn());
            });
        });


        MessageBus::Get().send(ThreadName::Input, [this]() {
            InputThread::getInstance().getKeyBindHandler().onPress(BindLayer::Global, {Keys::F7}, [this]() {
                if (uiSystem->isDebugModeOn())
                    uiSystem->logSelectedElementPosition();
            });
        });


        MessageBus::Get().send(ThreadName::Input, [this]() {
            InputThread::getInstance().getKeyBindHandler().onPress(BindLayer::Global, {Keys::F8}, [this]() {
                if (!profilingCapture_.isActive()) {
                    profilingCapture_.start();
                }
            });
        });

        MessageBus::Get().send(ThreadName::Input, [this]() {
            InputThread::getInstance().setMouseMoveCallback([this](double x, double y) {
                MessageBus::Get().send(ThreadName::Renderer, [this, x, y]() {
                    uiSystem->onMouseMove(x, y);
                });
            });
        });

        MessageBus::Get().send(ThreadName::Input, [this]() {
            InputThread::getInstance().setMouseButtonCallback([this](int button, int action, int mods) {
                double x = InputThread::getInstance().getLastX();
                double y = InputThread::getInstance().getLastY();
                MessageBus::Get().send(ThreadName::Renderer, [this, button, action, mods, x, y]() {
                    uiSystem->onMouseButton(button, action, mods, x, y);
                    if (screenManager->getCurrent())
                        screenManager->getCurrent()->onMouseButton(button, action, mods);
                });
            });
        });

        MessageBus::Get().send(ThreadName::Input, [this]() {
            InputThread::getInstance().setScrollCallback([this](double dx, double dy) {
                MessageBus::Get().send(ThreadName::Renderer, [this, dx, dy]() {
                    uiSystem->onScroll(dx, dy);
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


        MessageBus::Get().send(ThreadName::Input, [this]() {
            InputThread::getInstance().getKeyBindHandler().setUiKeyCallback([this](int key, int action) {
                uiSystem->onKey(key, action);
            });
        });


        MessageBus::Get().send(ThreadName::Input, [this]() {
            InputThread::getInstance().getKeyBindHandler().setUiCharCallback([this](unsigned int codepoint) {
                uiSystem->onChar(codepoint);
            });
        });

        uiSystem->registerButtonHandler(BTN_QUIT_GAME, []() {
            MessageBus::Get().send(ThreadName::Input, []() {
                InputThread::getInstance().setWindowClose();
            });
        });

        uiSystem->registerButtonHandler(BTN_ENTER_WORLD, [this]() {
            screenManager->switchTo<WorldScreen>(renderer->getExtent());
        });

        resourceManager->loadRawImageData("resources/textures/logo/Kingscraft-Logo.png",
            [](unsigned char* pixels, int width, int height) {
                MessageBus::Get().send(ThreadName::Input, [pixels, width, height]() {
                    InputThread::getInstance().setIcon(pixels, width, height);
                });
        });

        uiSystem->init(device, *descriptorManager_, renderer->getExtent());

        auto& appCtx = AppContext::get();
        appCtx.device = &device;
        appCtx.renderer = renderer.get();
        appCtx.uiSystem = uiSystem.get();
        appCtx.textureCache = textureCache_.get();
        appCtx.world = world_.get();
        appCtx.profilingCapture = &profilingCapture_;

        screenManager->switchTo<MainMenu>(renderer->getRenderPass(), *uiSystem, renderer->getExtent());

        VkExtent2D extent = InputThread::getInstance().getExtent().toVKExtent();
        postProcessor_ = std::make_unique<PostProcessing>();
        auto bloom = std::make_unique<Bloom>(device, extent, renderer->getWorldRenderPass(), *descriptorManager_);
        postProcessor_->addEffect(std::move(bloom));
    }

    App::~App() {
        vkDeviceWaitIdle(device.device());
    }

    bool App::windowShouldClose() const {
        return InputThread::getInstance().shouldClose();
    }

    void App::tick() {
        auto currentExtent = InputThread::getInstance().getExtent();

        if ((requestSwapchainRecreate || InputThread::getInstance().wasWindowResized()) && currentExtent.width > 0 && currentExtent.height > 0) {

            requestSwapchainRecreate = false;
            pauseRenderer();
            MessageBus::Get().send(ThreadName::Input, []() {
                InputThread::getInstance().resetWindowResizedFlag();
            });

            recreateSwapChain();
            uiSystem->resize(currentExtent.width, currentExtent.height);
            resumeRenderer();
        }

        if (renderState == RenderState::Running) {
            currentFrameStart_ = TimeUtil::uptimeSeconds();
            if (GameLogicThread::getInstance().isTickReady()) {
                GameLogicThread::getInstance().ackTick();
                profilingCapture_.tick(GameLogicThread::getInstance().getDt());
                drawFrame();
            }

            int maxFps = RendererSettings::get().maxFps;
            if (maxFps > 0) {
                double frameTime = TimeUtil::uptimeSeconds() - currentFrameStart_;
                double target = 1.0 / maxFps;
                if (frameTime < target) {
                    std::this_thread::sleep_for(std::chrono::duration<double>(target - frameTime));
                }
            }
        }
    }

    void App::cleanup() {
        vkDeviceWaitIdle(device.device());
    }

    void App::pauseRenderer() {
        renderState = RenderState::Paused;

        vkDeviceWaitIdle(device.device());
    }

    void App::resumeRenderer() {
        renderState = RenderState::Running;
    }

    void App::recreateSwapChain() {
        auto extent = InputThread::getInstance().getExtent().toVKExtent();
        while (extent.width == 0 || extent.height == 0) {
            MessageBus::Get().send(ThreadName::Input, []() {
                InputThread::getInstance().waitEvents();
            });
            extent = InputThread::getInstance().getExtent().toVKExtent();
        }

        vkDeviceWaitIdle(device.device());

        renderer->recreateSwapChain(extent);
        screenManager->notifyRenderPassChanged(renderer->getRenderPass());
        screenManager->notifySwapChainRecreated(extent);
        auto* bloom = static_cast<Bloom*>(postProcessor_->getEffect("bloom"));
        if (bloom) bloom->recreate(extent, renderer->getWorldRenderPass());
    }


    void App::drawFrame() {
        if (renderState != RenderState::Running) {
            return;
        }

        if (!renderer->beginFrame()) {
            recreateSwapChain();
            auto newExtent = InputThread::getInstance().getExtent();
            uiSystem->resize(newExtent.width, newExtent.height);
            return;
        }
        double cpuStart = TimeUtil::uptimeSeconds();
        VkCommandBuffer cmd = renderer->getActiveCommandBuffer();
        VkExtent2D extent = renderer->getExtent();
        uint32_t imageIndex = renderer->getCurrentImageIndex();

        auto info = screenManager->getCurrent()->getFrameRenderInfo(*renderer, imageIndex);

        if (info.uiEnabled) {
            uiSystem->update(GameLogicThread::getInstance().getDt());
            uint32_t qi = renderer->getFrameIndex() * Renderer::QUERIES_PER_FRAME;
            VkQueryPool tsPool = renderer->getGpuQueryPool();
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, tsPool, qi + Renderer::TS_UI_START);
            uiSystem->renderOffscreen(cmd);
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tsPool, qi + Renderer::TS_UI_END);
        }

        FrameContext frameCtx{};
        frameCtx.cmd = cmd;
        frameCtx.renderPass = info.renderPass;
        frameCtx.extent = extent;
        frameCtx.dt = GameLogicThread::getInstance().getDt();
        frameCtx.frameIndex = renderer->getFrameIndex();
        frameCtx.imageIndex = imageIndex;
        frameCtx.gpuQueryPool = renderer->getGpuQueryPool();
        frameCtx.postProcessing = postProcessor_.get();
        frameCtx.cpuFrameTimeMs = cpuFrameTimeMs_;
        frameCtx.cpuTickMs = GameLogicThread::getInstance().getCpuTickMs();
        frameCtx.cpuSubmitMs = cpuSubmitMs_;

        // Pre-scene effects (glow passes, downsampling, etc.)
        if (!info.uiEnabled) {
            postProcessor_->preScene(frameCtx, [this](const FrameContext& ctx) {
                screenManager->getCurrent()->renderGlow(ctx);
            });
        }

        RenderPassBegin pass{};
        pass.renderPass = info.renderPass;
        pass.framebuffer = info.framebuffer;
        pass.renderArea = {{0, 0}, extent};
        pass.viewport = {0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
        pass.scissor = {{0, 0}, extent};
        pass.clearValues = info.clearValues;

        renderer->executeRenderPass(pass, [this, frameCtx, info](VkCommandBuffer cb) {
            screenManager->render(frameCtx);

            // Post-scene effects (composite, etc.)
            if (!info.uiEnabled) {
                postProcessor_->postScene(frameCtx);
            }
        });

        cpuFrameTimeMs_ = (TimeUtil::uptimeSeconds() - cpuStart) * 1000.0;

        double submitStart = TimeUtil::uptimeSeconds();
        if (!renderer->endFrame()) {
            MessageBus::Get().send(ThreadName::Input, []() {
                InputThread::getInstance().resetWindowResizedFlag();
            });
            recreateSwapChain();
            auto newExtent = InputThread::getInstance().getExtent();
            uiSystem->resize(newExtent.width, newExtent.height);
        }
        cpuSubmitMs_ = (TimeUtil::uptimeSeconds() - submitStart) * 1000.0;
    }

}  // namespace lve
