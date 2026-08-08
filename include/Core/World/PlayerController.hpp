#pragma once

#include "Core/Camera.hpp"
#include "Core/World/Physics/AABB.hpp"
#include <vulkan/vulkan.h>

namespace lve {

    class KeyBindHandler;
    class Window;
    class World;

    class PlayerController {
    public:
        static constexpr float PLAYER_WIDTH = 0.6f;
        static constexpr float PLAYER_HEIGHT = 1.8f;
        static constexpr float EYE_HEIGHT = 1.62f;
        static constexpr float WALK_SPEED = 3.0f;

        void init(Camera& camera, KeyBindHandler& keybinds);
        void tick(double dt, World& world);
        void updateProjection(VkExtent2D extent, float fov, float nearPlane, float farPlane);

        Camera& getCamera() { return *camera_; }
        const Camera& getCamera() const { return *camera_; }
        glm::mat4 getViewProj() const;
        bool isCursorCaptured() const { return cursorCaptured_; }
        void setCaptured(bool captured) { cursorCaptured_ = captured; }
        void resetMouse();

        glm::vec3 getBodyPosition() const { return bodyPos_; }
        void setBodyPosition(const glm::vec3& pos) { bodyPos_ = pos; }
        AABB getBodyAABB() const {
            return AABB::fromPosition(bodyPos_,
                glm::vec3(PLAYER_WIDTH, PLAYER_HEIGHT, PLAYER_WIDTH));
        }

    private:
        Camera* camera_ = nullptr;
        KeyBindHandler* keybinds_ = nullptr;
        double lastMouseX_ = 0.0;
        double lastMouseY_ = 0.0;
        bool cursorCaptured_ = false;
        glm::vec3 bodyPos_{0.0f};
        bool spawnPending_ = true;
    };

} // namespace lve
