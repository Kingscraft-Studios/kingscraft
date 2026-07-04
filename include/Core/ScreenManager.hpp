#pragma once

#include "Core/Screen.hpp"
#include "Core/FrameContext.hpp"
#include <memory>

namespace lve {

    class ScreenManager {
    public:
        ScreenManager() = default;

        template<typename T, typename... Args>
        void switchTo(Args&&... args) {
            if (currentScreen_) currentScreen_->cleanup();
            currentScreen_ = std::make_unique<T>(std::forward<Args>(args)...);
            currentScreen_->init();
        }

        Screen* getCurrent() { return currentScreen_.get(); }

        void tick(double dt) { if (currentScreen_) currentScreen_->tick(dt); }
        void render(const FrameContext& ctx) { if (currentScreen_) currentScreen_->render(ctx); }
        void cleanup() { if (currentScreen_) { currentScreen_->cleanup(); currentScreen_.reset(); } }
        bool hasScreen() const { return currentScreen_ != nullptr; }
        void notifyRenderPassChanged(VkRenderPass rp) { if (currentScreen_) currentScreen_->onRenderPassChanged(rp); }
        void notifySwapChainRecreated(VkExtent2D extent) { if (currentScreen_) currentScreen_->onSwapChainRecreated(extent); }

    private:
        std::unique_ptr<Screen> currentScreen_;
    };

} // namespace lve
