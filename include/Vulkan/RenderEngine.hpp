#pragma once

#include "Vulkan/Device.hpp"
#include "Vulkan/DescriptorManager.hpp"
#include "Vulkan/TextureCache.hpp"
#include "UI/UiWrapper.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/PostProcessing.hpp"
#include "UI/Debug/ProfilingCapture.hpp"
#include <memory>

#include "Core/Runtime.hpp"
#include "Core/World/ChunkUploadProcessor.hpp"
#include "Renderer/WorldRenderer.hpp"
#include "Threads/InputThread.hpp"
#include "Util/TimeUtil.hpp"
#include <memory>

namespace kc {

    class Kingscraft;

    class RenderEngine {
    public:
        static constexpr int WIDTH = DEFAULT_WINDOW_WIDTH;
        static constexpr int HEIGHT = DEFAULT_WINDOW_HEIGHT;

        RenderEngine();
        ~RenderEngine();
        RenderEngine(const RenderEngine &) = delete;
        RenderEngine &operator=(const RenderEngine &) = delete;



        bool windowShouldClose() const;
        void tick();
        void cleanup();

        enum class RenderState {
            Running,
            Paused,
            Rebuilding
        };

        void pauseRenderer();
        void resumeRenderer();

        void swapchainRecreate(bool recreate) { requestSwapchainRecreate = recreate; }

        // Runs on the renderer thread (dispatched from the registry-reload
        // listener): rebuilds the texture array, world renderer, and UI block
        // texture, then forwards the game-thread half to Kingscraft.
        void refreshFromReload();

        Device& getDevice() { return device; }
        Renderer& getRenderer() { return *renderer; }
        UiWrapper& getUiSystem() { return *uiSystem; }
        ProfilingCapture& getProfileCapture() {return profilingCapture_;}
        PostProcessing& getPostProcessor() { return *postProcessor_; }
        TextureCache& getTextureCache() { return *textureCache_; }
        VkExtent2D getExtent() { return Runtime::get().inputThread->getExtent().toVKExtent(); }
        ChunkUploadProcessor& getChunkUploadProcessor() { return chunkProcessor; }
        double getCpuFrameTimeMs() const { return cpuFrameTimeMs_; }
        double getCpuSubmitMs() const { return cpuSubmitMs_; }

    private:
        void drawFrame(const FrameScene& scene);
        void recreateSwapChain();
        Device device;
        std::unique_ptr<Renderer> renderer = std::make_unique<Renderer>(device, Runtime::get().inputThread->getExtent().toVKExtent());
        std::unique_ptr<DescriptorManager> descriptorManager_ = std::make_unique<DescriptorManager>(device);
        std::unique_ptr<TextureCache> textureCache_ = std::make_unique<TextureCache>(device);
        std::unique_ptr<PostProcessing> postProcessor_;
        std::unique_ptr<UiWrapper> uiSystem = std::make_unique<UiWrapper>();
        ProfilingCapture profilingCapture_;
        ChunkUploadProcessor chunkProcessor;
        WorldRenderer worldRenderer;

        QueueFamilyIndices indices = device.findPhysicalQueueFamilies();
        bool requestSwapchainRecreate = false;
        double cpuFrameTimeMs_ = 0.0;
        double cpuSubmitMs_ = 0.0;
        double sleepIdleMs_ = 0.0;
        VkExtent2D lastExtent{0, 0};
        RenderState renderState = RenderState::Running;

        bool worldRendererInitialized = false;
    };

}  // namespace kc
