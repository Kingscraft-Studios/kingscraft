#pragma once

#include "Core/World/Physics/AABB.hpp"

namespace lve {

    class World;

    class CollisionSystem {
    public:
        static bool isSolidBlock(const World& world, int x, int y, int z);
        static bool aabbCollides(const World& world, const AABB& box);
        static void moveEntity(const World& world, AABB& box, glm::vec3& velocity, float dt);
    };

} // namespace lve
