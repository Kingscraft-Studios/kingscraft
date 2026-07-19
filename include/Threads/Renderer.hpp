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
        ScreenManager& getScreenManager() {
            return app->getScreenManager();
        }

    private:
        std::function<void()> quitCallback_;
        std::shared_ptr<Mailbox> mailbox_;
        std::unique_ptr<App> app = std::make_unique<App>();
    };

} // namespace lve
