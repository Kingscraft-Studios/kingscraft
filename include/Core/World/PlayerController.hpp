#pragma once

#include "Core/Camera.hpp"
#include <vulkan/vulkan.h>

namespace lve {

    class KeyBindHandler;
    class Window;

    class PlayerController {
    public:
        void init(Camera& camera, KeyBindHandler& keybinds);
        void tick(double dt);
        void updateProjection(VkExtent2D extent, float fov, float nearPlane, float farPlane);

        Camera& getCamera() { return *camera_; }
        const Camera& getCamera() const { return *camera_; }
        glm::mat4 getViewProj() const;
        bool isCursorCaptured() const { return cursorCaptured_; }
        void setCaptured(bool captured) { cursorCaptured_ = captured; }
        void resetMouse();

    private:
        Camera* camera_ = nullptr;
        KeyBindHandler* keybinds_ = nullptr;
        double lastMouseX_ = 0.0;
        double lastMouseY_ = 0.0;
        bool cursorCaptured_ = false;
    };

} // namespace lve
