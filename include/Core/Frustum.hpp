#pragma once

#include <glm/glm.hpp>
#include <array>

namespace kc {

struct Plane {
    glm::vec3 normal;
    float d;
};

class Frustum {
public:
    Frustum() = default;
    explicit Frustum(const glm::mat4& viewProj);

    void extract(const glm::mat4& viewProj);

    bool isVisible(const glm::vec3& min, const glm::vec3& max) const;
    bool isVisible(const glm::vec3& center, float radius) const;

private:
    std::array<Plane, 6> planes_{};
};

}
