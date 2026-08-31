#pragma once
#include <functional>
#include <memory>
#include "Bus/Mailbox.hpp"
#include "Vulkan/RenderEngine.hpp"

namespace kc {

    class RenderThread {
    public:
        static RenderThread& getInstance() {
            static RenderThread instance;
            return instance;
        }
        void setQuitCallback(std::function<void()> cb) { quitCallback_ = std::move(cb); }
        void run();
        void shutdown();
        bool isRunning() {
            return !app->windowShouldClose();
        }
        void recreateSwapchain(bool recreate) {
            app->swapchainRecreate(recreate);
        }
        UiWrapper& getUI() { return app->getUiSystem(); }
        ProfilingCapture& getProfilerCapture() { return app->getProfileCapture(); }
        Renderer& getRenderer() { return app->getRenderer(); }
        TextureCache& getTexCache() { return app->getTextureCache();}
        Device& getDevice() { return app->getDevice(); }
        ChunkUploadProcessor& getUploader() { return  app->getChunkUploadProcessor(); }
    private:
        std::function<void()> quitCallback_;
        std::shared_ptr<Mailbox> mailbox_;
        std::unique_ptr<RenderEngine> app = std::make_unique<RenderEngine>();
    };

} // namespace kc
