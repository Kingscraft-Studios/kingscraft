#include "Core/World/Physics/CollisionSystem.hpp"

#include "Core/Blocks/Block.hpp"
#include "Core/Registry.hpp"
#include "Core/World/World.hpp"
#include <cmath>

namespace lve {

    bool CollisionSystem::isSolidBlock(const World& world, int x, int y, int z) {
        if (y < 0 || y >= world.getHeight()) return false;

        uint8_t blockId = world.getBlock(x, y, z);
        if (blockId == 0) return false;

        const Block* block = Registry<Block>::getRegistry().get(blockId);
        return block != nullptr && block->isSolid();
    }

    bool CollisionSystem::aabbCollides(const World& world, const AABB& box) {
        int x0 = static_cast<int>(std::floor(box.min.x));
        int x1 = static_cast<int>(std::ceil(box.max.x)) - 1;
        int y0 = static_cast<int>(std::floor(box.min.y));
        int y1 = static_cast<int>(std::ceil(box.max.y)) - 1;
        int z0 = static_cast<int>(std::floor(box.min.z));
        int z1 = static_cast<int>(std::ceil(box.max.z)) - 1;

        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                    if (isSolidBlock(world, x, y, z)) return true;

        return false;
    }

    void CollisionSystem::moveEntity(const World& world, AABB& box, glm::vec3& velocity, float dt) {
        const glm::vec3 delta = velocity * dt;
        const glm::vec3 size = box.getSize();

        if (delta.x != 0.0f) {
            box = box.translate({delta.x, 0.0f, 0.0f});
            if (aabbCollides(world, box)) {
                if (delta.x > 0.0f) {
                    box.max.x = std::floor(box.max.x);
                    box.min.x = box.max.x - size.x;
                } else {
                    box.min.x = std::ceil(box.min.x);
                    box.max.x = box.min.x + size.x;
                }
                velocity.x = 0.0f;
            }
        }

        if (delta.z != 0.0f) {
            box = box.translate({0.0f, 0.0f, delta.z});
            if (aabbCollides(world, box)) {
                if (delta.z > 0.0f) {
                    box.max.z = std::floor(box.max.z);
                    box.min.z = box.max.z - size.z;
                } else {
                    box.min.z = std::ceil(box.min.z);
                    box.max.z = box.min.z + size.z;
                }
                velocity.z = 0.0f;
            }
        }

        if (delta.y != 0.0f) {
            box = box.translate({0.0f, delta.y, 0.0f});
            if (aabbCollides(world, box)) {
                if (delta.y > 0.0f) {
                    box.max.y = std::floor(box.max.y);
                    box.min.y = box.max.y - size.y;
                } else {
                    box.min.y = std::ceil(box.min.y);
                    box.max.y = box.min.y + size.y;
                }
                velocity.y = 0.0f;
            }
        }
    }

} // namespace lve
