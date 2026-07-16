#pragma once
#include <memory>
#include <optional>

#include "Bus/Mailbox.hpp"
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
            return window->shouldClose();
        }
        WindowExtent getExtent() {
            return window->getExtent();
        }
        bool wasWindowResized() {
            return window->wasWindowResized();
        }
        void resetWindowResizedFlag() {
            window->resetWindowResizedFlag();
        }
        void processInput() {
            window->processInput();
        }
        void toggleFullscreen() {
            window->toggleFullscreen();
        }
        double getLastX() const {
            return window->getLastX();
        }
        double getLastY() const {
            return window->getLastY();
        }
        void setWindowClose() {
            window->setWindowClose();
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

        void run();
        void Shutdown();

    private:
        std::optional<Window> window;
        std::shared_ptr<Mailbox> mailbox;
        std::atomic<bool> running;
        std::atomic<bool> initialized{false};
    };
}