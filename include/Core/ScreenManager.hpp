#pragma once

#include "Core/Screen.hpp"
#include <functional>
#include <memory>
#include <tuple>
#include <utility>

namespace kc {

    class ScreenManager {
    public:
        ScreenManager() = default;
        // Screens own raw pointers registered in UiWrapper::elements_; destroying
        // a screen without cleanup() leaves dangling pointers that the render
        // thread's final tick happily dereferences (startup/shutdown race). Always
        // tear the active screen down before it is freed.
        ~ScreenManager() { cleanup(); }

        template<typename T, typename... Args>
        void setScreen(Args&&... args) {
            if (currentScreen_) currentScreen_->cleanup();
            currentScreen_ = std::make_unique<T>(std::forward<Args>(args)...);
            currentScreen_->init();
        }

        // Defers a screen transition until the current tick() has returned.
        // Use this when the switch is requested from inside the active screen's
        // own tick/handler: the old screen must not be destroyed while its
        // method is still on the stack.
        template<typename T, typename... Args>
        void queueScreen(Args&&... args) {
            // tuple<Args...> preserves lvalue references (e.g. ActionRouter&)
            // and stores by value otherwise.
            auto stored = std::tuple<Args...>(std::forward<Args>(args)...);
            pending_ = [stored = std::move(stored)]() mutable -> std::unique_ptr<Screen> {
                return std::apply([](auto&&... unpacked) -> std::unique_ptr<Screen> {
                    return std::make_unique<T>(
                        std::forward<decltype(unpacked)>(unpacked)...);
                }, std::move(stored));
            };
        }

        Screen* getCurrent() { return currentScreen_.get(); }

        void tick(double dt) {
            if (currentScreen_) currentScreen_->tick(dt);
            applyPending();
        }
        void render(FrameScene& scene) { if (currentScreen_) currentScreen_->render(scene); }
        void cleanup() { if (currentScreen_) { currentScreen_->cleanup(); currentScreen_.reset(); } }
        bool hasScreen() const { return currentScreen_ != nullptr; }

    private:
        void applyPending() {
            if (!pending_) return;
            auto factory = std::move(pending_);
            pending_ = nullptr;
            if (currentScreen_) {
                currentScreen_->cleanup();
                currentScreen_.reset();
            }
            currentScreen_ = factory();
            if (currentScreen_) currentScreen_->init();
        }

        std::unique_ptr<Screen> currentScreen_;
        std::function<std::unique_ptr<Screen>()> pending_;
    };

} // namespace kc
