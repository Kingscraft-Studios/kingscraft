#include "Core/World/PlayerController.hpp"
#include "Core/KeyBindHandler.hpp"
#include "Core/Keys.hpp"
#include "Core/World/Physics/CollisionSystem.hpp"
#include "Core/World/World.hpp"
#include "Threads/InputThread.hpp"
#include "Vulkan/Window.hpp"
#include <cmath>

namespace lve {

    void PlayerController::init(Camera& camera, KeyBindHandler& keybinds) {
        camera_ = &camera;
        keybinds_ = &keybinds;
        lastMouseX_ = InputThread::getInstance().getLastX();
        lastMouseY_ = InputThread::getInstance().getLastY();
    }

    void PlayerController::tick(double dt, World& world) {
        if (!camera_ || !keybinds_ || !InputThread::getInstance().isInitialized()) return;
        if (!cursorCaptured_) return;

        if (spawnPending_) {
            int surface = world.getSurfaceHeight(
                static_cast<int>(std::floor(bodyPos_.x)),
                static_cast<int>(std::floor(bodyPos_.z)));
            if (surface >= 0) {
                bodyPos_.y = static_cast<float>(surface) + 1.0f;
                spawnPending_ = false;
            }
        }

        const float dtf = static_cast<float>(dt);
        const glm::vec3 forward = camera_->getForward();
        const glm::vec3 forwardH = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
        const glm::vec3 rightH = glm::normalize(glm::cross(forwardH, glm::vec3(0.0f, 1.0f, 0.0f)));

        glm::vec3 velocity(0.0f);
        if (keybinds_->isDown(Keys::W)) velocity += forwardH * WALK_SPEED;
        if (keybinds_->isDown(Keys::S)) velocity -= forwardH * WALK_SPEED;
        if (keybinds_->isDown(Keys::A)) velocity -= rightH * WALK_SPEED;
        if (keybinds_->isDown(Keys::D)) velocity += rightH * WALK_SPEED;
        if (keybinds_->isDown(Keys::SPACE)) velocity.y += WALK_SPEED;
        if (keybinds_->isDown(Keys::LEFT_SHIFT)) velocity.y -= WALK_SPEED;

        AABB box = AABB::fromPosition(bodyPos_,
            glm::vec3(PLAYER_WIDTH, PLAYER_HEIGHT, PLAYER_WIDTH));
        CollisionSystem::moveEntity(world, box, velocity, dtf);
        bodyPos_ = box.min;

        camera_->setPosition(bodyPos_ + glm::vec3(PLAYER_WIDTH * 0.5f, EYE_HEIGHT, PLAYER_WIDTH * 0.5f));

        double mx = InputThread::getInstance().getLastX();
        double my = InputThread::getInstance().getLastY();
        double dx = mx - lastMouseX_;
        double dy = lastMouseY_ - my;
        lastMouseX_ = mx;
        lastMouseY_ = my;

        if (dx != 0.0 || dy != 0.0) {
            camera_->rotate(static_cast<float>(dx) * 0.1f, static_cast<float>(dy) * 0.1f);
        }
    }

    void PlayerController::updateProjection(VkExtent2D extent, float fov, float nearPlane, float farPlane) {
        if (!camera_) return;
        float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        camera_->setAspectRatio(aspect);
        camera_->setFov(fov);
        camera_->setNearPlane(nearPlane);
        camera_->setFarPlane(farPlane);
    }

    void PlayerController::resetMouse() {
        if (InputThread::getInstance().isInitialized()) {
            lastMouseX_ = InputThread::getInstance().getLastX();
            lastMouseY_ = InputThread::getInstance().getLastY();
        }
    }

    glm::mat4 PlayerController::getViewProj() const {
        if (!camera_) return glm::mat4(1.0f);
        return camera_->getProjectionMatrix() * camera_->getViewMatrix();
    }

} // namespace lve
