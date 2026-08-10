#pragma once

#include "Core/World/Physics/AABB.hpp"
#include <cstdint>
#include <optional>

namespace lve {

    class World;
    class Block;

    class CollisionSystem {
    public:
        static bool isSolidBlock(const World& world, int x, int y, int z);
        static const AABB& getBlockCollisionBox(uint8_t blockId);
        static AABB blockAABBAt(uint8_t blockId, int x, int y, int z);
        static AABB blockAABBAt(const Block& block, int x, int y, int z);
        static std::optional<AABB> getWorldBlockAABB(const World& world, int x, int y, int z);
        static bool aabbCollides(const World& world, const AABB& box);
        static void moveEntity(const World& world, AABB& box, glm::vec3& velocity, float dt);
    };

} // namespace lve
