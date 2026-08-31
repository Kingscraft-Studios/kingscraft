
#pragma once

#define GLFW_INCLUDE_VULKAN

#include "GLFW/glfw3.h"

#include <string>
#include <functional>

#include "Core/WindowStruct.hpp"

namespace kc {

    class GLFWWindow {
    public:
        GLFWWindow(int w, int h, const std::string& name);

        ~GLFWWindow();

        GLFWWindow(const GLFWWindow &) = delete;

        GLFWWindow &operator=(const GLFWWindow &) = delete;

        bool shouldClose() const { return glfwWindowShouldClose(window); }

        WindowExtent getExtent() { return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}; }

        bool wasWindowResized() { return framebufferResized; }

        void resetWindowResizedFlag() { framebufferResized = false; }

        void createWindowSurface(VkInstance instance, VkSurfaceKHR *surface);
        GLFWwindow* getGLFWWindow() const { return window; }

        void toggleFullscreen();

        using MouseMovementCallback = std::function<void(double, double)>;
        using MouseButtonCallback = std::function<void(int, int, int)>;
        using ScrollCallback = std::function<void(double, double)>;
        using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
        using CharCallback = std::function<void(unsigned int codepoint)>;

        void setMouseMoveCallback(MouseMovementCallback cb) { mouseMovementCallback = std::move(cb); }
        void setMouseButtonCallback(MouseButtonCallback cb) { mouseButtonCallback = std::move(cb); }
        void setScrollCallback(ScrollCallback cb) { scrollCallback = std::move(cb); }
        void setKeyCallback(KeyCallback cb) { keyCallback = std::move(cb); }
        void setCharCallback(CharCallback cb) { charCallback = std::move(cb); }

        void setFullscreenToggleCallback(std::function<void()> callback) {
            fullscreenToggleCallback = std::move(callback);
        }

        // Helper to get last mouse pos for click logic
        double getLastX() const { return lastX; }
        double getLastY() const { return lastY; }
        void setWindowClose() {glfwSetWindowShouldClose(window, GLFW_TRUE);}
        void setIcon(unsigned char* pixels, int width, int height);

        void setCursorType(int mode, int value) { glfwSetInputMode(window, mode, value); }

        void setClipboard(const char* string) {
            glfwSetClipboardString(window, string);
        }

        void pollGLFWEvents() {
            glfwPollEvents();
        }

        void waitEvents() {
            glfwWaitEvents();
        }

        using ResizeHook = std::function<void(int width, int height)>;
        using MousePosHook = std::function<void(double x, double y)>;
        void setResizeHook(ResizeHook hook) { resizeHook_ = std::move(hook); }
        void setMousePosHook(MousePosHook hook) { mousePosHook_ = std::move(hook); }

    private:
        static void framebufferResizeCallback(GLFWwindow *window, int width, int height);

        void initWindow();

        int width;
        int height;
        bool framebufferResized = false;

        std::string windowName;
        GLFWwindow *window;

        bool isFullscreen = false;
        int windowedX = 100, windowedY = 100;
        int windowedWidth;
        int windowedHeight;

        GLFWmonitor* monitor = nullptr;
        const GLFWvidmode* videoMode = nullptr;

        std::function<void()> fullscreenToggleCallback;

        MouseMovementCallback mouseMovementCallback;
        MouseButtonCallback mouseButtonCallback;
        ScrollCallback scrollCallback;
        KeyCallback keyCallback;
        CharCallback charCallback;

        ResizeHook resizeHook_;
        MousePosHook mousePosHook_;

        double lastX = 0, lastY = 0;
    };
}  // namespace kc