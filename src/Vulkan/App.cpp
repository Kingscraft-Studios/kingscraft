#include "Vulkan/App.hpp"
#include "Core/MainMenu.hpp"
#include "Core/World/WorldScreen.hpp"
#include "Renderer/Bloom.hpp"
#include "Renderer/RendererSettings.hpp"
#include <chrono>
#include <thread>

#include "Bus/MessageBus.hpp"
#include "Renderer/FrameScene.hpp"
#include "Threads/InputThread.hpp"
#include "Threads/Engine.hpp"
#include "Util/LogUtils.hpp"

namespace lve {

    App::App() {

        resourceManager->loadRawImageData("resources/textures/logo/Kingscraft-Logo.png",
            [](unsigned char* pixels, int width, int height) {
                MessageBus::Get().send(ThreadName::Input, [pixels, width, height]() {
                    InputThread::getInstance().setIcon(pixels, width, height);
                });
        });

        uiSystem->init(device, *descriptorManager_, renderer->getExtent());

        // TODO: Move this Into GameLogic
        MessageBus::Get().send(ThreadName::GameLogic, [this]() {
            GameLogicThread::getInstance().setScreen<MainMenu>(renderer->getRenderPass(), *uiSystem, renderer->getExtent());
        });

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
                profilingCapture_.tick(GameLogicThread::getInstance().getDelta());
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
        // screenManager->notifyRenderPassChanged(renderer->getRenderPass());
        // screenManager->notifySwapChainRecreated(extent);
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

        const FrameScene& scene = Engine::Get().getFrameExchange().readFrame();
        RenderTarget target = renderer->buildRenderTarget(scene);

        if (scene.ui.enabled) {
            uiSystem->update(scene.stats.delta);
            uint32_t qi = renderer->getFrameIndex() * Renderer::QUERIES_PER_FRAME;
            VkQueryPool tsPool = renderer->getGpuQueryPool();
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, tsPool, qi + Renderer::TS_UI_START);
            uiSystem->renderOffscreen(cmd);
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tsPool, qi + Renderer::TS_UI_END);
        }

        FrameContext frameCtx{};
        frameCtx.cmd = cmd;
        frameCtx.renderPass = target.renderPass;
        frameCtx.extent = extent;
        frameCtx.dt = scene.stats.delta;
        frameCtx.frameIndex = renderer->getFrameIndex();
        frameCtx.imageIndex = imageIndex;
        frameCtx.gpuQueryPool = renderer->getGpuQueryPool();
        frameCtx.postProcessing = postProcessor_.get();
        frameCtx.cpuFrameTimeMs = cpuFrameTimeMs_;
        frameCtx.cpuTickMs = scene.stats.cpuTickMs;
        frameCtx.cpuSubmitMs = cpuSubmitMs_;

        // Pre-scene effects (glow passes, downsampling, etc.)
        if (!scene.ui.enabled) {
            postProcessor_->preScene(frameCtx, [](const FrameContext& ctx) {
                // screenManager->getCurrent()->renderGlow(ctx);
            });
        }

        RenderPassBegin pass{};
        pass.renderPass = target.renderPass;
        pass.framebuffer = target.framebuffer;
        pass.renderArea = {{0, 0}, extent};
        pass.viewport = {0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
        pass.scissor = {{0, 0}, extent};
        pass.clearValues = target.clearValues;
        pass.clearCount = target.clearCount;

        renderer->executeRenderPass(pass, [this, frameCtx, scene, target](VkCommandBuffer cb) {
            // Temp
            uiSystem->render(cb,target.renderPass);

            // Post-scene effects (composite, etc.)
            if (!scene.ui.enabled) {
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
