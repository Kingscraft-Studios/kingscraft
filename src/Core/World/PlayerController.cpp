#include "Core/World/PlayerController.hpp"
#include "Core/KeyBindHandler.hpp"
#include "Core/Keys.hpp"
#include "Core/World/Physics/CollisionSystem.hpp"
#include "Core/World/World.hpp"
#include "Threads/InputThread.hpp"
#include "Vulkan/Window.hpp"
#include <cmath>

namespace lve {

    namespace {
        constexpr float VOID_KILL_Y = -32.0f;
    }

    void PlayerController::init(KeyBindHandler& keybinds) {
        keybinds_ = &keybinds;
        lastMouseX_ = InputThread::getInstance().getLastX();
        lastMouseY_ = InputThread::getInstance().getLastY();
        bodyPos_ = spawnPos_;
        velocityY_ = 0.0f;

        camera_.setPosition(bodyPos_ + glm::vec3(Attributes::PLAYER_WIDTH * 0.5f, Attributes::EYE_HEIGHT, Attributes::PLAYER_WIDTH * 0.5f));
        camera_.setRotation(0.0f, -35.0f);

        keybinds_->onPress(BindLayer::Screen, {Keys::SPACE}, [this]() {
            if (cursorCaptured_) jumpRequested_ = true;
        });
    }

    void PlayerController::tick(World& world, double dt) {
        if (!keybinds_ || !InputThread::getInstance().isInitialized()) return;
        if (dead_) return;
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
        const glm::vec3 forward = camera_.getForward();
        const glm::vec3 forwardH = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
        const glm::vec3 rightH = glm::normalize(glm::cross(forwardH, glm::vec3(0.0f, 1.0f, 0.0f)));

        glm::vec3 velocity(0.0f);
        AABB box = AABB::fromPosition(bodyPos_,
            glm::vec3(Attributes::PLAYER_WIDTH, Attributes::PLAYER_HEIGHT, Attributes::PLAYER_WIDTH));
        if (respawnGrace_ > 0.0f) {
            respawnGrace_ -= dtf;
            jumpRequested_ = false;
        } else {
            if (keybinds_->isDown(Keys::W)) velocity += forwardH * Attributes::WALK_SPEED;
            if (keybinds_->isDown(Keys::S)) velocity -= forwardH * Attributes::WALK_SPEED;
            if (keybinds_->isDown(Keys::A)) velocity -= rightH * Attributes::WALK_SPEED;
            if (keybinds_->isDown(Keys::D)) velocity += rightH * Attributes::WALK_SPEED;

            if (CollisionSystem::aabbCollides(world, box.translate(glm::vec3(0.0f, -0.05f, 0.0f)))) {
                velocityY_ = 0.0f;
                if (jumpRequested_) {
                    jumpRequested_ = false;
                    velocityY_ = Attributes::JUMP_STRENGTH;
                }
            }
        }
        velocity.y = velocityY_;

        CollisionSystem::moveEntity(world, box, velocity, dtf);
        velocityY_ = velocity.y;
        bodyPos_ = box.min;

        if (bodyPos_.y < VOID_KILL_Y) {
            dead_ = true;
            velocityY_ = 0.0f;
            jumpRequested_ = false;
            return;
        }

        camera_.setPosition(bodyPos_ + glm::vec3(Attributes::PLAYER_WIDTH * 0.5f, Attributes::EYE_HEIGHT, Attributes::PLAYER_WIDTH * 0.5f));

        double mx = InputThread::getInstance().getLastX();
        double my = InputThread::getInstance().getLastY();
        double dx = mx - lastMouseX_;
        double dy = lastMouseY_ - my;
        lastMouseX_ = mx;
        lastMouseY_ = my;

        if (dx != 0.0 || dy != 0.0) {
            camera_.rotate(static_cast<float>(dx) * 0.1f, static_cast<float>(dy) * 0.1f);
        }
    }

    void PlayerController::respawn() {
        bodyPos_ = spawnPos_;
        velocityY_ = 0.0f;
        respawnGrace_ = 0.4f;
        spawnPending_ = true;
        dead_ = false;
    }

    void PlayerController::updateProjection(VkExtent2D extent, float fov, float nearPlane, float farPlane) {
        float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        camera_.setAspectRatio(aspect);
        camera_.setFov(fov);
        camera_.setNearPlane(nearPlane);
        camera_.setFarPlane(farPlane);
    }

    void PlayerController::resetMouse() {
        if (InputThread::getInstance().isInitialized()) {
            lastMouseX_ = InputThread::getInstance().getLastX();
            lastMouseY_ = InputThread::getInstance().getLastY();
        }
    }

    glm::mat4 PlayerController::getViewProj() const {
        return camera_.getProjectionMatrix() * camera_.getViewMatrix();
    }

} // namespace lve
