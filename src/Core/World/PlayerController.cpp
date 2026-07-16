#include "Core/World/PlayerController.hpp"
#include "Core/KeyBindHandler.hpp"
#include "Core/Keys.hpp"
#include "Threads/InputThread.hpp"
#include "Vulkan/Window.hpp"

namespace lve {

    void PlayerController::init(Camera& camera, KeyBindHandler& keybinds) {
        camera_ = &camera;
        keybinds_ = &keybinds;
        lastMouseX_ = InputThread::getInstance().getLastX();
        lastMouseY_ = InputThread::getInstance().getLastY();
    }

    void PlayerController::tick(double dt) {
        if (!camera_ || !keybinds_ || !InputThread::getInstance().isInitialized()) return;
        if (!cursorCaptured_) return;

        float speed = 3.0f * static_cast<float>(dt);
        if (keybinds_->isDown(Keys::W)) camera_->moveForward(speed);
        if (keybinds_->isDown(Keys::S)) camera_->moveForward(-speed);
        if (keybinds_->isDown(Keys::A)) camera_->moveRight(-speed);
        if (keybinds_->isDown(Keys::D)) camera_->moveRight(speed);
        if (keybinds_->isDown(Keys::SPACE)) camera_->moveUp(speed);
        if (keybinds_->isDown(Keys::LEFT_SHIFT)) camera_->moveUp(-speed);

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
