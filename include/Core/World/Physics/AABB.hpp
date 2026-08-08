#pragma once

#include <glm/glm.hpp>

namespace lve {

    struct AABB {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};

        AABB() = default;
        AABB(glm::vec3 min, glm::vec3 max) : min(min), max(max) {}

        static AABB fromPosition(const glm::vec3& pos, const glm::vec3& size) {
            return AABB(pos, pos + size);
        }

        AABB translate(const glm::vec3& delta) const {
            return AABB(min + delta, max + delta);
        }

        glm::vec3 getSize() const { return max - min; }
    };

} // namespace lve
