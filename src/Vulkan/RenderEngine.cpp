#include "Vulkan/RenderEngine.hpp"
#include "Core/MainMenu.hpp"
#include "Core/World/WorldScreen.hpp"
#include "Renderer/Bloom.hpp"
#include "Renderer/RendererSettings.hpp"
#include <chrono>
#include <thread>

#include "Bus/MessageBus.hpp"
#include "Core/Registries.hpp"
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
            // The cache always has a texture now (real atlas or fallback), so
            // bind it immediately instead of leaving the UI on its placeholder.
            uiSystem->setBlockTexture(textureCache_->getImageView(), textureCache_->getSampler());
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
            double tickStart = TimeUtil::uptimeSeconds();

            // Idle between the previous frame's end and this frame's start: the
            // render thread's outer loop (mailbox drain, window pump). Measured
            // here and not inside drawFrame so it cannot be confused with work.
            gpuMetrics_.gapMs =
                lastFrameEnd_ > 0.0 ? (tickStart - lastFrameEnd_) * 1000.0 : 0.0;

            const FrameScene& scene = Engine::Get().getFrameExchange().readFrame();

            // Everything between entering tick() and having a scene to record,
            // minus the swapchain checks: this is the only stretch of the frame
            // that no other counter covers.
            gpuMetrics_.mailboxMs = (TimeUtil::uptimeSeconds() - tickStart) * 1000.0;

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

            // Measured at the END of the frame, so this snapshot's interval and
            // all of its parts describe the same frame. The old code sampled the
            // interval at the start, which paired frame N-1's period with frame
            // N's costs and made the remainder meaningless (it could go
            // negative when one frame was fast and the next stalled).
            const double frameEnd = TimeUtil::uptimeSeconds();
            gpuMetrics_.workMs = (frameEnd - tickStart) * 1000.0;
            gpuMetrics_.frameIntervalMs = gpuMetrics_.workMs + gpuMetrics_.gapMs;
            // Tail after the queue submit: collectDestroyedChunks, the profiling
            // tick, the max-Fps sleep and the metrics publish itself.
            gpuMetrics_.postMs = gpuMetrics_.workMs - gpuMetrics_.mailboxMs
                               - gpuMetrics_.beginMs - gpuMetrics_.cpuFrameMs
                               - gpuMetrics_.cpuSubmitMs;
            lastFrameEnd_ = frameEnd;

            // Hand this frame's render metrics to the engine thread. If the
            // reader is mid-copy and no buffer is free, skip this frame; the
            // next one will publish.
            MetricsExchange& metricsExchange = Engine::Get().getMetricsExchange();
            if (RenderThreadMetrics* gpu = metricsExchange.writeMetrics()) {
                *gpu = gpuMetrics_;
                metricsExchange.publish();
            }
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
        // Routed to the Renderer thread by the dispatcher (via the
        // RenderRegistryReloadListener), before the frame: the block set and its
        // texture array changed, so the descriptor layout bound to the old
        // pipeline layout is stale. Idle the GPU and rebuild everything the
        // initial init created. The game-side remesh runs on the GameLogic
        // mailbox once this listener completes (dispatch waits on it).
        vkDeviceWaitIdle(device.device());

        textureCache_->updateFromRegistry();
        worldRenderer.cleanup();
        worldRenderer.init(device, *textureCache_, renderer->getWorldRenderPass());
        uiSystem->setBlockTexture(textureCache_->getImageView(), textureCache_->getSampler());
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
            gpuMetrics_.skippedFrames++;
            return;
        }

        double beginStart = TimeUtil::uptimeSeconds();
        if (!renderer->beginFrame()) {
            gpuMetrics_.skippedFrames++;
            recreateSwapChain();
            auto newExtent = Runtime::get().inputThread->getExtent();
            uiSystem->resize(newExtent.width, newExtent.height);
            return;
        }
        // beginFrame() does more than wait (query readback, staging advance,
        // command pool reset). CIdle only covers the fence and acquire, so this
        // total vs CIdle is how the untimed tail there becomes visible.
        double cpuStart = TimeUtil::uptimeSeconds();
        gpuMetrics_.beginMs = (cpuStart - beginStart) * 1000.0;
        gpuMetrics_.frames++;
        VkCommandBuffer cmd = renderer->getActiveCommandBuffer();
        VkExtent2D extent = renderer->getExtent();
        uint32_t imageIndex = renderer->getCurrentImageIndex();

        // The UI is drawn straight into the world pass after the scene, so
        // there is nothing to composite and no extra fullscreen pass.
        RenderTarget target = renderer->buildRenderTarget(scene);

        if (scene.ui.enabled) {
            uiSystem->update(scene.stats.delta);
            uiSystem->build();
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
                worldRenderer.render(cb, scene.camera.viewProj, scene,
                    renderer->getGpuQueryPool(),
                    renderer->getFrameIndex() * Renderer::QUERIES_PER_FRAME + Renderer::TS_OPAQUE_END);
            } else if (VkQueryPool tsPool = renderer->getGpuQueryPool()) {
                // No terrain this frame: still write the opaque-end timestamp so
                // the query range stays fully available and the GPU metrics do
                // not freeze on menu screens.
                vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tsPool,
                    renderer->getFrameIndex() * Renderer::QUERIES_PER_FRAME + Renderer::TS_OPAQUE_END);
            }

            // End of the 3D scene (terrain + water + highlight), before the UI.
            if (VkQueryPool tsPool = renderer->getGpuQueryPool()) {
                vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, tsPool,
                    renderer->getFrameIndex() * Renderer::QUERIES_PER_FRAME + Renderer::TS_SCENE_END);
            }

            // UI draws into this pass directly (depth test off), replacing the
            // offscreen target + fullscreen composite (~0.58 ms of pure copy).
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

        RenderThreadMetrics& gpuMetrics = gpuMetrics_;
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
        gpuMetrics.presentMs = renderer->getPresentMs();
        gpuMetrics.opaqueGpuMs = renderer->getOpaqueGpuMs();
        gpuMetrics.transparentGpuMs = renderer->getTransparentGpuMs();
        gpuMetrics.terrainGpuMs = renderer->getTerrainGpuMs();
    }

}  // namespace kc
