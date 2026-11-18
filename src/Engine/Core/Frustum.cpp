#include "Core/Frustum.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace kc {

Frustum::Frustum(const glm::mat4& viewProj) {
    extract(viewProj);
}

void Frustum::extract(const glm::mat4& m) {
    // Vulkan clip space: x=[-1,1], y=[-1,1], z=[0,1]
    //
    // Plane from M = P * V:  n·p + d = 0
    //   Left:   row3 + row0      (x_clip = -w_clip)
    //   Right:  row3 - row0      (x_clip =  w_clip)
    //   Bottom: row3 + row1      (y_clip = -w_clip)
    //   Top:    row3 - row1      (y_clip =  w_clip)
    //   Near:   row2             (z_clip = 0)
    //   Far:    row3 - row2      (z_clip = w_clip)
    //
    // GLM is column-major: m[col][row] = element at column col, row row.
    // Row r = { m[0][r], m[1][r], m[2][r], m[3][r] }

    planes_[0] = { glm::vec3(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0]), m[3][3] + m[3][0] };
    planes_[1] = { glm::vec3(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0]), m[3][3] - m[3][0] };
    planes_[2] = { glm::vec3(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1]), m[3][3] + m[3][1] };
    planes_[3] = { glm::vec3(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1]), m[3][3] - m[3][1] };
    planes_[4] = { glm::vec3(m[0][2],           m[1][2],           m[2][2]          ), m[3][2]           };
    planes_[5] = { glm::vec3(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2]), m[3][3] - m[3][2] };

    for (auto& p : planes_) {
        float len = glm::length(p.normal);
        if (len > 0.0f) {
            p.normal /= len;
            p.d /= len;
        }
    }
}

bool Frustum::isVisible(const glm::vec3& min, const glm::vec3& max) const {
    for (const auto& p : planes_) {
        glm::vec3 pv{
            p.normal.x >= 0.0f ? max.x : min.x,
            p.normal.y >= 0.0f ? max.y : min.y,
            p.normal.z >= 0.0f ? max.z : min.z,
        };
        if (glm::dot(p.normal, pv) + p.d < 0.0f)
            return false;
    }
    return true;
}

bool Frustum::isVisible(const glm::vec3& center, float radius) const {
    for (const auto& p : planes_) {
        if (glm::dot(p.normal, center) + p.d < -radius)
            return false;
    }
    return true;
}

}
