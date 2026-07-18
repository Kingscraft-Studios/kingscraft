#pragma once
#include <atomic>
#include <memory>
#include <optional>

#include "Bus/Mailbox.hpp"
#include "Core/KeyBindHandler.hpp"
#include "Core/WindowStruct.hpp"
#include "Vulkan/Window.hpp"

namespace lve {
    class InputThread {
    public:
        static InputThread& getInstance() {
            static InputThread instance;
            return instance;
        }

        void Init(WindowCreateInfo info);

        bool shouldClose() const {
            return closeRequested_.load(std::memory_order_acquire);
        }
        WindowExtent getExtent() {
            return {cachedWidth_.load(std::memory_order_acquire),
                    cachedHeight_.load(std::memory_order_acquire)};
        }
        bool wasWindowResized() {
            return windowResized_.load(std::memory_order_acquire);
        }
        void resetWindowResizedFlag() {
            window->resetWindowResizedFlag();
            windowResized_.store(false, std::memory_order_release);
        }
        void toggleFullscreen() {
            window->toggleFullscreen();
        }
        double getLastX() const {
            return lastMouseX_.load(std::memory_order_acquire);
        }
        double getLastY() const {
            return lastMouseY_.load(std::memory_order_acquire);
        }
        void setWindowClose() {
            window->setWindowClose();
            closeRequested_.store(true, std::memory_order_release);
        }
        void setCursorType(int mode, int value) {
            window->setCursorType(mode, value);
        }
        void setIcon(unsigned char* pixels, int width, int height) {
            window->setIcon(pixels, width, height);
        }

        using MouseMovementCallback = Window::MouseMovementCallback;
        using MouseButtonCallback = Window::MouseButtonCallback;
        using ScrollCallback = Window::ScrollCallback;
        using KeyCallback = Window::KeyCallback;
        using CharCallback = Window::CharCallback;

        void setMouseMoveCallback(MouseMovementCallback cb) {
            window->setMouseMoveCallback(std::move(cb));
        }
        void setMouseButtonCallback(MouseButtonCallback cb) {
            window->setMouseButtonCallback(std::move(cb));
        }
        void setScrollCallback(ScrollCallback cb) {
            window->setScrollCallback(std::move(cb));
        }
        void setKeyCallback(KeyCallback cb) {
            window->setKeyCallback(std::move(cb));
        }
        void setCharCallback(CharCallback cb) {
            window->setCharCallback(std::move(cb));
        }
        void setFullscreenToggleCallback(std::function<void()> callback) {
            window->setFullscreenToggleCallback(std::move(callback));
        }
        void createSurface(VkInstance instance, VkSurfaceKHR& surface) {
            window->createWindowSurface(instance, &surface);
        }
        bool isInitialized() {
            return initialized.load(std::memory_order_acquire);
        }
        void setClipBoard(const std::string text) {
            window->setClipboard(text.c_str());
        }
        void waitEvents() {
            window->waitEvents();
        }

        KeyBindHandler& getKeyBindHandler() {
            return *keyHandler;
        }

        void run();
        void Shutdown();

    private:
        std::optional<Window> window;
        std::shared_ptr<Mailbox> mailbox;
        std::atomic<bool> running;
        std::atomic<bool> initialized{false};
        std::unique_ptr<KeyBindHandler> keyHandler = std::make_unique<KeyBindHandler>();

        std::atomic<bool> closeRequested_{false};
        std::atomic<uint32_t> cachedWidth_{0};
        std::atomic<uint32_t> cachedHeight_{0};
        std::atomic<bool> windowResized_{false};
        std::atomic<double> lastMouseX_{0.0};
        std::atomic<double> lastMouseY_{0.0};
    };
}