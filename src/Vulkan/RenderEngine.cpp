#include "Vulkan/RenderEngine.hpp"
#include "Core/MainMenu.hpp"
#include "Core/World/WorldScreen.hpp"
#include "Renderer/Bloom.hpp"
#include "Renderer/RendererSettings.hpp"
#include <chrono>
#include <thread>

#include "Bus/MessageBus.hpp"
#include "Core/Registries.hpp"
#include "Event/EventManager.hpp"
#include "Event/Events/RegistryReloadPostEvent.hpp"
#include "Renderer/FrameScene.hpp"
#include "Threads/InputThread.hpp"
#include "Threads/Engine.hpp"
#include "Util/ScopedTimer.hpp"

namespace kc {

    RenderEngine::RenderEngine() {
        uiSystem->init(device, *descriptorManager_, renderer->getExtent());

        VkExtent2D extent = Runtime::get().inputThread->getExtent().toVKExtent();
        postProcessor_ = std::make_unique<PostProcessing>();
        auto bloom = std::make_unique<Bloom>(device, extent, renderer->getWorldRenderPass(), *descriptorManager_);
        postProcessor_->addEffect(std::move(bloom));
    }

    RenderEngine::~RenderEngine() {
        chunkProcessor.cleanup(device);
        vkDeviceWaitIdle(device.device());
    }

    bool RenderEngine::windowShouldClose() const {
        return Runtime::get().inputThread->shouldClose();
    }

    void RenderEngine::tick() {
        if (!worldRendererInitialized) {
            Registries::waitForBuild();
            textureCache_->updateFromRegistry();
            worldRenderer.init(device, *textureCache_, renderer->getWorldRenderPass());
            worldRendererInitialized = true;
        }

        auto currentExtent = Runtime::get().inputThread->getExtent();

        if ((requestSwapchainRecreate || Runtime::get().inputThread->wasWindowResized()) && currentExtent.width > 0 && currentExtent.height > 0) {

            requestSwapchainRecreate = false;
            pauseRenderer();
            MessageBus::Get().send(ThreadName::Input, []() {
                Runtime::get().inputThread->resetWindowResizedFlag();
            });

            recreateSwapChain();
            uiSystem->resize(currentExtent.width, currentExtent.height);
            resumeRenderer();
        }

        if (renderState == RenderState::Running) {
            const FrameScene& scene = Engine::Get().getFrameExchange().readFrame();
            drawFrame(scene);
            chunkProcessor.collectDestroyedChunks(scene.terrain.draws);
            profilingCapture_.tick(scene.stats.delta);

            int maxFps = RendererSettings::get().maxFps;
            if (maxFps > 0) {
                double target = 1.0 / maxFps;
                if (scene.stats.delta < target) {
                    sleepIdleMs_ = 0.0;
                    {
                        ScopedTimer t(sleepIdleMs_);
                        std::this_thread::sleep_for(std::chrono::duration<double>(target - scene.stats.delta));
                    }
                } else {
                    sleepIdleMs_ = 0.0;
                }
            }
            Engine::Get().getFrameExchange().endRead();
        }
    }

    void RenderEngine::cleanup() {
        vkDeviceWaitIdle(device.device());
    }

    void RenderEngine::pauseRenderer() {
        renderState = RenderState::Paused;

        vkDeviceWaitIdle(device.device());
    }

    void RenderEngine::resumeRenderer() {
        renderState = RenderState::Running;
    }

    void RenderEngine::refreshFromReload() {
        // Runs inside the renderer mailbox, before the frame: the block set and
        // its texture array changed, so the descriptor layout bound to the old
        // pipeline layout is stale. Idle the GPU and rebuild everything the
        // initial init created, then hand the game thread its half.
        vkDeviceWaitIdle(device.device());

        textureCache_->updateFromRegistry();
        worldRenderer.cleanup();
        worldRenderer.init(device, *textureCache_, renderer->getWorldRenderPass());
        uiSystem->setBlockTexture(textureCache_->getImageView(), textureCache_->getSampler());

        // Offsets are patched and the renderer is rebuilt: hand the game thread
        // its half by firing the post event there (a safe handoff, never raced).
        MessageBus::Get().send(ThreadName::GameLogic, []() {
            EventManager::get().callEvent<RegistryReloadPostEvent>();
        });
    }

    void RenderEngine::recreateSwapChain() {
        auto extent = Runtime::get().inputThread->getExtent().toVKExtent();
        while (extent.width == 0 || extent.height == 0) {
            MessageBus::Get().send(ThreadName::Input, []() {
                Runtime::get().inputThread->waitEvents();
            });
            extent = Runtime::get().inputThread->getExtent().toVKExtent();
        }

        vkDeviceWaitIdle(device.device());

        renderer->recreateSwapChain(extent);
        // screenManager->notifyRenderPassChanged(renderer->getRenderPass());
        worldRenderer.onRenderPassChanged(renderer->getWorldRenderPass());
        // screenManager->notifySwapChainRecreated(extent);
        auto* bloom = static_cast<Bloom*>(postProcessor_->getEffect("bloom"));
        if (bloom) bloom->recreate(extent, renderer->getWorldRenderPass());
    }


    void RenderEngine::drawFrame(const FrameScene& scene) {
        if (renderState != RenderState::Running) {
            return;
        }

        if (!renderer->beginFrame()) {
            recreateSwapChain();
            auto newExtent = Runtime::get().inputThread->getExtent();
            uiSystem->resize(newExtent.width, newExtent.height);
            return;
        }
        double cpuStart = TimeUtil::uptimeSeconds();
        Engine::Get().getDiagnostics().getGPUMetrics().frames++;
        VkCommandBuffer cmd = renderer->getActiveCommandBuffer();
        VkExtent2D extent = renderer->getExtent();
        uint32_t imageIndex = renderer->getCurrentImageIndex();

        RenderTarget target = renderer->buildRenderTarget(scene);

        if (scene.ui.enabled) {
            uiSystem->update(scene.stats.delta);
            uint32_t qi = renderer->getFrameIndex() * Renderer::QUERIES_PER_FRAME;
            VkQueryPool tsPool = renderer->getGpuQueryPool();
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, tsPool, qi + Renderer::TS_UI_START);
            uiSystem->renderOffscreen(cmd, renderer->getFrameIndex());
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
        // if (!scene.ui.enabled) {
        //     postProcessor_->preScene(frameCtx, [](const FrameContext& ctx) {
        //         // screenManager->getCurrent()->renderGlow(ctx);
        //     });
        // }

        RenderPassBegin pass{};
        pass.renderPass = target.renderPass;
        pass.framebuffer = target.framebuffer;
        pass.renderArea = {{0, 0}, extent};
        pass.viewport = {0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
        pass.scissor = {{0, 0}, extent};
        pass.clearValues = target.clearValues;
        pass.clearCount = target.clearCount;

        renderer->executeRenderPass(pass, [this, frameCtx, scene, target](VkCommandBuffer cb) {
            if (scene.terrain.renderTerrain) {
                worldRenderer.render(cb, scene.camera.viewProj, scene);
            }
            if (scene.ui.enabled) {
                uiSystem->render(cb, target.renderPass, renderer->getFrameIndex());
            }

            // Post-scene effects (composite, etc.)
            // if (!scene.ui.enabled) {
            //     postProcessor_->postScene(frameCtx);
            // }
        });

        cpuFrameTimeMs_ = (TimeUtil::uptimeSeconds() - cpuStart) * 1000.0;

        double submitStart = TimeUtil::uptimeSeconds();
        if (!renderer->endFrame()) {
            MessageBus::Get().send(ThreadName::Input, []() {
                Runtime::get().inputThread->resetWindowResizedFlag();
            });
            recreateSwapChain();
            auto newExtent = Runtime::get().inputThread->getExtent();
            uiSystem->resize(newExtent.width, newExtent.height);
        }
        cpuSubmitMs_ = (TimeUtil::uptimeSeconds() - submitStart) * 1000.0;

        auto& gpuMetrics = Engine::Get().getDiagnostics().getGPUMetrics();
        gpuMetrics.cpuFrameMs = cpuFrameTimeMs_;
        gpuMetrics.cpuSubmitMs = cpuSubmitMs_;
        gpuMetrics.cmdRecordMs = renderer->getCmdRecordMs();
        gpuMetrics.gpuFrameMs = renderer->getGpuFrameTimeMs();
        gpuMetrics.worldGpuMs = renderer->getWorldGpuMs();
        gpuMetrics.uiGpuMs = renderer->getUiGpuMs();
        gpuMetrics.memBandwidthGBs = renderer->getMemBandwidthGBs();
        gpuMetrics.overdraw = renderer->getOverdraw();
        gpuMetrics.pipeline.iaVertices = renderer->getPipelineStat(Renderer::STAT_IA_VERTICES);
        gpuMetrics.pipeline.iaPrimitives = renderer->getPipelineStat(Renderer::STAT_IA_PRIMITIVES);
        gpuMetrics.pipeline.vsInvocations = renderer->getPipelineStat(Renderer::STAT_VS_INVOCATIONS);
        gpuMetrics.pipeline.fsInvocations = renderer->getPipelineStat(Renderer::STAT_FS_INVOCATIONS);
        gpuMetrics.pipeline.clipInvocations = renderer->getPipelineStat(Renderer::STAT_CLIP_INVOC);
        gpuMetrics.pipeline.clipPrims = renderer->getPipelineStat(Renderer::STAT_CLIP_PRIMS);
        gpuMetrics.cIdle = renderer->getCIdleMs() + sleepIdleMs_;
    }

}  // namespace kc
