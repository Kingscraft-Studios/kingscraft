#pragma once

#include "Core/Screen.hpp"
#include <memory>

namespace lve {

    class ScreenManager {
    public:
        ScreenManager() = default;

        template<typename T, typename... Args>
        void setScreen(Args&&... args) {
            if (currentScreen_) currentScreen_->cleanup();
            currentScreen_ = std::make_unique<T>(std::forward<Args>(args)...);
            currentScreen_->init();
        }

        Screen* getCurrent() { return currentScreen_.get(); }

        void tick(double dt) { if (currentScreen_) currentScreen_->tick(dt); }
        void render(FrameScene& scene) { if (currentScreen_) currentScreen_->render(scene); }
        void cleanup() { if (currentScreen_) { currentScreen_->cleanup(); currentScreen_.reset(); } }
        bool hasScreen() const { return currentScreen_ != nullptr; }

    private:
        std::unique_ptr<Screen> currentScreen_;
    };

} // namespace lve
