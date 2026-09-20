#include "Core/World/PlayerController.hpp"
#include "Core/KeyBindHandler.hpp"
#include "Core/Keys.hpp"
#include "Core/World/Physics/CollisionSystem.hpp"
#include "Core/World/World.hpp"
#include "Event/EventManager.hpp"
#include "Event/Events/PlayerDeathEvent.hpp"
#include "Event/Events/PlayerRespawnEvent.hpp"
#include "Threads/InputThread.hpp"
#include "Vulkan/GLFWWindow.hpp"
#include <cmath>
#include <vector>
#include "Util/TimeUtil.hpp"
#include "Core/Runtime.hpp"

namespace kc {

    namespace {
        constexpr float VOID_KILL_Y = -32.0f;
    }

    void PlayerController::init(KeyBindHandler& keybinds) {
        keybinds_ = &keybinds;
        lastMouseX_ = Runtime::get().inputThread->getLastX();
        lastMouseY_ = Runtime::get().inputThread->getLastY();
        bodyPos_ = spawnPos_;
        velocityY_ = 0.0f;

        camera_.setPosition(bodyPos_ + glm::vec3(Attributes::PLAYER_WIDTH * 0.5f, Attributes::EYE_HEIGHT, Attributes::PLAYER_WIDTH * 0.5f));
        camera_.setRotation(0.0f, -35.0f);

        keybinds_->onPress(BindLayer::Screen, {Keys::SPACE}, [this]() {
            if (cursorCaptured_) jumpPresses_.push_back(TimeUtil::uptimeSeconds());
        });
    }

    void PlayerController::tick(World& world, double dt) {
        if (!keybinds_ || !Runtime::get().inputThread->isInitialized()) return;
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
            jumpPresses_.clear();
        } else if (flyMode_) {
            // Consume the press log: a double-tap mid-air restores gravity.
            for (double ts : jumpPresses_) {
                if (ts - lastFlyTap_ < FLY_TOGGLE_WINDOW) {
                    flyMode_ = false;
                    jumpedThisAirTime_ = false;
                    lastFlyTap_ = -1.0e9;
                } else {
                    lastFlyTap_ = ts;
                }
            }
            jumpPresses_.clear();

            // Gravity is suppressed by World.cpp while flying; pilot vertical.
            if (keybinds_->isDown(Keys::W)) velocity += forwardH * Attributes::FLY_SPEED;
            if (keybinds_->isDown(Keys::S)) velocity -= forwardH * Attributes::FLY_SPEED;
            if (keybinds_->isDown(Keys::A)) velocity -= rightH * Attributes::FLY_SPEED;
            if (keybinds_->isDown(Keys::D)) velocity += rightH * Attributes::FLY_SPEED;
            if (keybinds_->isDown(Keys::SPACE)) {
                velocityY_ = Attributes::FLY_CLIMB_SPEED;
            } else if (keybinds_->isDown(Keys::LEFT_SHIFT) || keybinds_->isDown(Keys::RIGHT_SHIFT)) {
                velocityY_ = -Attributes::FLY_DESCEND_SPEED;
            } else {
                velocityY_ = 0.0f;
            }
        } else {
            if (keybinds_->isDown(Keys::W)) velocity += forwardH * Attributes::WALK_SPEED;
            if (keybinds_->isDown(Keys::S)) velocity -= forwardH * Attributes::WALK_SPEED;
            if (keybinds_->isDown(Keys::A)) velocity -= rightH * Attributes::WALK_SPEED;
            if (keybinds_->isDown(Keys::D)) velocity += rightH * Attributes::WALK_SPEED;

            bool grounded = CollisionSystem::aabbCollides(world, box.translate(glm::vec3(0.0f, -0.05f, 0.0f)));
            if (grounded) {
                velocityY_ = 0.0f;
                jumpedThisAirTime_ = false;
                if (!jumpPresses_.empty()) {
                    velocityY_ = Attributes::JUMP_STRENGTH;
                    jumpedThisAirTime_ = true;
                }
            } else if (!jumpPresses_.empty() && jumpedThisAirTime_) {
                // DOUBLE JUMP: in the air after a real jump -> gravity OFF.
                flyMode_ = true;
                jumpedThisAirTime_ = false;
                velocityY_ = Attributes::FLY_CLIMB_SPEED;
            }
            jumpPresses_.clear();
        }
        velocity.y = velocityY_;

        CollisionSystem::moveEntity(world, box, velocity, dtf);
        velocityY_ = velocity.y;
        bodyPos_ = box.min;

        if (bodyPos_.y < VOID_KILL_Y) {
            dead_ = true;
            velocityY_ = 0.0f;
            flyMode_ = false;
            jumpedThisAirTime_ = false;
            jumpPresses_.clear();
            EventManager::get().callEvent<PlayerDeathEvent>(DeathCause::Void);
            return;
        }

        camera_.setPosition(bodyPos_ + glm::vec3(Attributes::PLAYER_WIDTH * 0.5f, Attributes::EYE_HEIGHT, Attributes::PLAYER_WIDTH * 0.5f));

        double mx = Runtime::get().inputThread->getLastX();
        double my = Runtime::get().inputThread->getLastY();
        double dx = mx - lastMouseX_;
        double dy = lastMouseY_ - my;
        lastMouseX_ = mx;
        lastMouseY_ = my;

        if (dx != 0.0 || dy != 0.0) {
            camera_.rotate(static_cast<float>(dx) * 0.1f, static_cast<float>(dy) * 0.1f);
        }
    }

    void PlayerController::setSpawn(const glm::vec3& spawn) {
        spawnPos_ = spawn;
    }

    void PlayerController::restore(const glm::vec3& position, float yaw, float pitch, bool snapToSurface) {
        bodyPos_ = position;
        camera_.setPosition(bodyPos_ + glm::vec3(Attributes::PLAYER_WIDTH * 0.5f, Attributes::EYE_HEIGHT, Attributes::PLAYER_WIDTH * 0.5f));
        camera_.setRotation(yaw, pitch);
        // Only a fresh default spawn needs the surface snap; a persisted
        // position keeps its exact Y (e.g. inside a dug-out room / box).
        spawnPending_ = snapToSurface;
        flyMode_ = false;
        jumpedThisAirTime_ = false;
        jumpPresses_.clear();
        lastFlyTap_ = -1.0e9;
    }

    void PlayerController::respawn() {
        bodyPos_ = spawnPos_;
        velocityY_ = 0.0f;
        respawnGrace_ = 0.4f;
        spawnPending_ = true;
        dead_ = false;
        flyMode_ = false;
        jumpedThisAirTime_ = false;
        jumpPresses_.clear();
        lastFlyTap_ = -1.0e9;
        EventManager::get().callEvent<PlayerRespawnEvent>();
    }

    void PlayerController::updateProjection(VkExtent2D extent, float fov, float nearPlane, float farPlane) {
        float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        camera_.setAspectRatio(aspect);
        camera_.setFov(fov);
        camera_.setNearPlane(nearPlane);
        camera_.setFarPlane(farPlane);
    }

    void PlayerController::resetMouse() {
        if (Runtime::get().inputThread->isInitialized()) {
            lastMouseX_ = Runtime::get().inputThread->getLastX();
            lastMouseY_ = Runtime::get().inputThread->getLastY();
        }
    }

    glm::mat4 PlayerController::getViewProj() const {
        return camera_.getProjectionMatrix() * camera_.getViewMatrix();
    }

} // namespace kc
