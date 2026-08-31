#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace kc {

    class Camera {
    public:
        void setPosition(glm::vec3 pos) { position_ = pos; }
        glm::vec3 getPosition() const { return position_; }
        glm::vec3 getForward() const { return forward_; }

        void setRotation(float yaw, float pitch) { yaw_ = yaw; pitch_ = pitch; updateVectors(); }

        void moveForward(float amount) {
            glm::vec3 dir = glm::normalize(glm::vec3(forward_.x, 0.0f, forward_.z));
            position_ += dir * amount;
        }
        void moveRight(float amount) { position_ += right_ * amount; }
        void moveUp(float amount) { position_.y += amount; }

        void rotate(float yawDelta, float pitchDelta) {
            yaw_ += yawDelta;
            pitch_ += pitchDelta;
            if (pitch_ > 89.0f) pitch_ = 89.0f;
            if (pitch_ < -89.0f) pitch_ = -89.0f;
            updateVectors();
        }

        glm::mat4 getViewMatrix() const {
            return glm::lookAt(position_, position_ + forward_, up_);
        }

        glm::mat4 getProjectionMatrix() const {
            glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(fov_), aspectRatio_, near_, far_);
            proj[1][1] *= -1;
            return proj;
        }

        void setAspectRatio(float aspect) { aspectRatio_ = aspect; }
        void setFov(float fov) { fov_ = fov; }
        void setNearPlane(float near) { near_ = near; }
        void setFarPlane(float far) { far_ = far; }

    private:
        void updateVectors() {
            glm::vec3 f;
            f.x = cos(glm::radians(yaw_)) * cos(glm::radians(pitch_));
            f.y = sin(glm::radians(pitch_));
            f.z = sin(glm::radians(yaw_)) * cos(glm::radians(pitch_));
            forward_ = glm::normalize(f);
            right_ = glm::normalize(glm::cross(forward_, glm::vec3(0.0f, 1.0f, 0.0f)));
            up_ = glm::normalize(glm::cross(right_, forward_));
        }

        glm::vec3 position_{0.0f, 0.0f, 3.0f};
        glm::vec3 forward_{0.0f, 0.0f, -1.0f};
        glm::vec3 up_{0.0f, 1.0f, 0.0f};
        glm::vec3 right_{1.0f, 0.0f, 0.0f};
        float yaw_ = -90.0f;
        float pitch_ = 0.0f;
        float fov_ = 60.0f;
        float near_ = 0.1f;
        float far_ = 1000.0f;
        float aspectRatio_ = 16.0f / 9.0f;
    };

} // namespace kc
