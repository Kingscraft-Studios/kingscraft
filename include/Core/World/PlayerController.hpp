#pragma once

#include "Core/Camera.hpp"
#include "Core/World/Physics/AABB.hpp"
#include <vulkan/vulkan.h>

#include "Core/Attributes.hpp"

namespace lve {

    class KeyBindHandler;
    class Window;
    class World;

    class PlayerController {
    public:

        void init(KeyBindHandler& keybinds);
        void tick(World& world, double dt);
        void updateProjection(VkExtent2D extent, float fov, float nearPlane, float farPlane);

        Camera& getCamera() { return camera_; }
        const Camera& getCamera() const { return camera_; }
        glm::mat4 getViewProj() const;
        bool isCursorCaptured() const { return cursorCaptured_; }
        void setCaptured(bool captured) { cursorCaptured_ = captured; }
        void resetMouse();

        glm::vec3 getBodyPosition() const { return bodyPos_; }
        float getVelocityY() const { return velocityY_; }
        void setVelocityY(float v) { velocityY_ = v; }
        AABB getBodyAABB() const {
            return AABB::fromPosition(bodyPos_,
                glm::vec3(Attributes::PLAYER_WIDTH, Attributes::PLAYER_HEIGHT, Attributes::PLAYER_WIDTH));
        }

    private:
        Camera camera_;
        KeyBindHandler* keybinds_ = nullptr;
        double lastMouseX_ = 0.0;
        double lastMouseY_ = 0.0;
        bool cursorCaptured_ = false;
        glm::vec3 bodyPos_{0.0f};
        glm::vec3 spawnPos_{67.5f, 15.0f - Attributes::EYE_HEIGHT, 67.5f};
        float velocityY_ = 0.0f;
        float respawnGrace_ = 0.0f;
        bool jumpRequested_ = false;
        bool spawnPending_ = true;
    };

} // namespace lve
