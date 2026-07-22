#pragma once
#include <functional>
#include <memory>
#include "Bus/Mailbox.hpp"
#include "Vulkan/App.hpp"

namespace lve {

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
        World& getWorld() { return app->getWorld(); }
        TextureCache& getTexCache() { return app->getTextureCache();}
        Device& getDevice() { return app->getDevice(); }
    private:
        std::function<void()> quitCallback_;
        std::shared_ptr<Mailbox> mailbox_;
        std::unique_ptr<App> app = std::make_unique<App>();
    };

} // namespace lve
