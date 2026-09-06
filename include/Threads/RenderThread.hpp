#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

#include "BaseThread.hpp"
#include "Bus/Mailbox.hpp"
#include "Vulkan/RenderEngine.hpp"

namespace kc {

    class RenderThread : public BaseThread{
    public:
        void start() override;
        void run() override;
        void stop() override;
        void signalQuit() override;

        void setQuitCallback(std::function<void()> cb) { quitCallback_ = std::move(cb); }
        void recreateSwapchain(bool recreate) {
            app->swapchainRecreate(recreate);
        }

        void requestTeardown();

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

        std::atomic<bool> teardownRequested = false;
        std::mutex teardownMtx;
        std::condition_variable teardownCv;
    };

} // namespace kc
