#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace kc {

    class World;

    struct RaycastHit {
        int x = 0, y = 0, z = 0;
        int face = 0;
        bool hit = false;
    };

    RaycastHit raycastBlock(const glm::vec3& origin, const glm::vec3& dir,
                            float maxDist, const World& world);

}
