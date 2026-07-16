#include "Vulkan/App.hpp"
#include "Core/MainMenu.hpp"
#include "Core/World/WorldScreen.hpp"
#include "Renderer/Bloom.hpp"
#include "Renderer/RendererSettings.hpp"
#include <chrono>
#include <thread>

#include "Threads/InputThread.hpp"

namespace lve {

    App::App() {

        keybinds_->onPress(BindLayer::Global, {Keys::F11}, [this]() {
            InputThread::getInstance().toggleFullscreen();
            requestSwapchainRecreate = true;
        });

        keybinds_->onPress(BindLayer::Global, {Keys::ESCAPE}, [this]() {
            InputThread::getInstance().setWindowClose();
        });

        keybinds_->onPress(BindLayer::Global, {Keys::F3, Keys::F6}, [this]() {
            uiSystem->setDebugMode(!uiSystem->isDebugModeOn());
        });

        keybinds_->onPress(BindLayer::Global, {Keys::F7}, [this]() {
            if (uiSystem->isDebugModeOn())
                uiSystem->logSelectedElementPosition();
        });

        keybinds_->onPress(BindLayer::Global, {Keys::F8}, [this]() {
            if (!profilingCapture_.isActive()) {
                profilingCapture_.start();
            }
        });

        InputThread::getInstance().setMouseMoveCallback([this](double x, double y) {
            uiSystem->onMouseMove(x, y);
        });

        InputThread::getInstance().setMouseButtonCallback([this](int button, int action, int mods) {
            double x = InputThread::getInstance().getLastX();
            double y = InputThread::getInstance().getLastY();

            uiSystem->onMouseButton(button, action, mods, x, y);

            if (screenManager->getCurrent())
                screenManager->getCurrent()->onMouseButton(button, action, mods);
        });

        InputThread::getInstance().setScrollCallback([this](double dx, double dy) {
            uiSystem->onScroll(dx, dy);
        });

        InputThread::getInstance().setKeyCallback([this](int key, int scancode, int action, int mods) {
            keybinds_->onKeyEvent(key, scancode, action, mods);
        });

        InputThread::getInstance().setCharCallback([this](unsigned int codepoint) {
            keybinds_->onChar(codepoint);
        });

        keybinds_->setUiKeyCallback([this](int key, int action) {
            uiSystem->onKey(key, action);
        });

        keybinds_->setUiCharCallback([this](unsigned int codepoint) {
            uiSystem->onChar(codepoint);
        });

        uiSystem->registerButtonHandler(BTN_QUIT_GAME, [this]() {
            InputThread::getInstance().setWindowClose();
        });

        uiSystem->registerButtonHandler(BTN_ENTER_WORLD, [this]() {
            screenManager->switchTo<WorldScreen>(renderer->getExtent());
        });

        resourceManager->loadRawImageData("resources/textures/logo/Kingscraft-Logo.png",
            [this](unsigned char* pixels, int width, int height) {
                InputThread::getInstance().setIcon(pixels, width, height);
            });

        // Init new UI engine alongside Noesis
        uiSystem->init(device, *descriptorManager_, renderer->getExtent());

        auto& appCtx = AppContext::get();
        appCtx.device = &device;
        appCtx.renderer = renderer.get();
        appCtx.uiSystem = uiSystem.get();
        appCtx.keybinds = keybinds_.get();
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
        currentTime = TimeUtil::uptimeSeconds();
        dt_ = currentTime - prevTime_;
        prevTime_ = currentTime;
        if (dt_ > 0.25) dt_ = 0.25;

        glfwPollEvents();
        keybinds_->update();
        InputThread::getInstance().processInput();

        auto currentExtent = InputThread::getInstance().getExtent();

        if ((requestSwapchainRecreate || InputThread::getInstance().wasWindowResized()) && currentExtent.width > 0 && currentExtent.height > 0) {

            requestSwapchainRecreate = false;
            pauseRenderer();
            InputThread::getInstance().resetWindowResizedFlag();

            recreateSwapChain();
            uiSystem->resize(currentExtent.width, currentExtent.height);
            resumeRenderer();
        }

        if (renderState == RenderState::Running) {
            tickAccumulator_ += dt_;
            double tickStart = TimeUtil::uptimeSeconds();
            while (tickAccumulator_ >= TICK_INTERVAL) {
                screenManager->tick(TICK_INTERVAL);
                tickAccumulator_ -= TICK_INTERVAL;
            }
            cpuTickMs_ = (TimeUtil::uptimeSeconds() - tickStart) * 1000.0;

            profilingCapture_.tick(dt_);

            drawFrame();

            int maxFps = RendererSettings::get().maxFps;
            if (maxFps > 0) {
                double frameTime = TimeUtil::uptimeSeconds() - currentTime;
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
            extent = InputThread::getInstance().getExtent().toVKExtent();
            glfwWaitEvents();
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
            uiSystem->update(dt_);
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
        frameCtx.dt = dt_;
        frameCtx.frameIndex = renderer->getFrameIndex();
        frameCtx.imageIndex = imageIndex;
        frameCtx.gpuQueryPool = renderer->getGpuQueryPool();
        frameCtx.postProcessing = postProcessor_.get();
        frameCtx.cpuFrameTimeMs = cpuFrameTimeMs_;
        frameCtx.cpuTickMs = cpuTickMs_;
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
            InputThread::getInstance().resetWindowResizedFlag();
            recreateSwapChain();
            auto newExtent = InputThread::getInstance().getExtent();
            uiSystem->resize(newExtent.width, newExtent.height);
        }
        cpuSubmitMs_ = (TimeUtil::uptimeSeconds() - submitStart) * 1000.0;
    }

}  // namespace lve
