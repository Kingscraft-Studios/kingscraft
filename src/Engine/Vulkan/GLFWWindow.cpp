#include "Vulkan/GLFWWindow.hpp"

#include <stdexcept>

namespace kc {

    GLFWWindow::GLFWWindow(int w, int h, const std::string& name) : width{w}, height{h}, windowName{name} {
        initWindow();
    }

    GLFWWindow::~GLFWWindow() {
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void GLFWWindow::initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        window = glfwCreateWindow(width, height, windowName.c_str(), nullptr, nullptr);

        glfwGetWindowPos(window, &windowedX, &windowedY);
        glfwGetWindowSize(window, &windowedWidth, &windowedHeight);

        monitor = glfwGetPrimaryMonitor();
        videoMode = glfwGetVideoMode(monitor);


        glfwSetWindowUserPointer(window, this);

        glfwSetCursorPosCallback(window, [](GLFWwindow* w, double xpos, double ypos) {
        auto win = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w));
        if (win) {
            win->lastX = xpos;
            win->lastY = ypos;
            if (win->mousePosHook_) win->mousePosHook_(xpos, ypos);

            if (win->mouseMovementCallback) {
                win->mouseMovementCallback(xpos, ypos);
            }
        }
    });

        glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int button, int action, int mods) {
        auto kcWin = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w));
        if (kcWin && kcWin->mouseButtonCallback) {
            kcWin->mouseButtonCallback(button, action, mods);
        }
    });

        glfwSetScrollCallback(window, [](GLFWwindow* w, double xoffset, double yoffset) {
            auto win = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w));
            if (win && win->scrollCallback)
                win->scrollCallback(xoffset, yoffset);
        });

        glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int scancode, int action, int mods) {
            auto win = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w));
            if (win && win->keyCallback) {
                win->keyCallback(key, scancode, action, mods);
            }
        });

        glfwSetCharCallback(window, [](GLFWwindow* w, unsigned int codepoint) {
            auto win = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(w));
            if (win && win->charCallback) {
                win->charCallback(codepoint);
            }
        });

        glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    }

    void GLFWWindow::createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) {
        if (glfwCreateWindowSurface(instance, window, nullptr, surface) != VK_SUCCESS) {
            throw std::runtime_error("failed to create window surface!");
        }
    }

    void GLFWWindow::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
        auto kcWindow = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
        if (!kcWindow) return;

        kcWindow->framebufferResized = true;
        kcWindow->width = width;
        kcWindow->height = height;
        if (kcWindow->resizeHook_) kcWindow->resizeHook_(width, height);
    }

    void GLFWWindow::toggleFullscreen() {
        if (!isFullscreen) {
            // Save the current window position and size
            glfwGetWindowPos(window, &windowedX, &windowedY);
            glfwGetWindowSize(window, &windowedWidth, &windowedHeight);

            // Switch to fullscreen on the primary monitor
            glfwSetWindowMonitor(window, monitor, 0, 0, videoMode->width, videoMode->height, videoMode->refreshRate);
            isFullscreen = true;
        } else {
            // Restore to windowed mode
            glfwSetWindowMonitor(window, nullptr, windowedX, windowedY, windowedWidth, windowedHeight, 0);
            isFullscreen = false;
        }
    }

    void GLFWWindow::setIcon(unsigned char* pixels, int width, int height) {
        GLFWimage icon[1];
        icon[0].width = width;
        icon[0].height = height;
        icon[0].pixels = pixels;
        glfwSetWindowIcon(window, 1, icon);
    }

}  // namespace kc