#pragma once

#include "Core/World/Physics/AABB.hpp"
#include <cstdint>
#include <optional>

namespace kc {

    class World;
    class Block;

    class CollisionSystem {
    public:
        static bool isSolidBlock(const World& world, int x, int y, int z);
        static const AABB& getBlockCollisionBox(uint64_t blockId);
        static AABB blockAABBAt(uint64_t blockId, int x, int y, int z);
        static AABB blockAABBAt(const Block& block, int x, int y, int z);
        static std::optional<AABB> getWorldBlockAABB(const World& world, int x, int y, int z);
        static bool aabbCollides(const World& world, const AABB& box);

        // Moves `box` by `velocity * dt`, resolving each axis in turn and then
        // depenetrating.
        //
        // Returns false when the box still overlaps solid ground when it is done,
        // which happens when the entity is wedged with no free cell above it and
        // every horizontal escape blocked. Callers used to ignore this and the
        // entity was then simply frozen inside terrain with no way out, so the
        // return value is the caller's cue to recover.
        static bool moveEntity(const World& world, AABB& box, glm::vec3& velocity, float dt);
    };

} // namespace kc
