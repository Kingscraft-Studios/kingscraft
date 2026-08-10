#include "Core/World/Physics/CollisionSystem.hpp"

#include "Core/Blocks/Block.hpp"
#include "Core/Registry.hpp"
#include "Core/World/World.hpp"
#include <cmath>

namespace lve {

    bool CollisionSystem::isSolidBlock(const World& world, int x, int y, int z) {
        return getWorldBlockAABB(world, x, y, z).has_value();
    }

    const AABB& CollisionSystem::getBlockCollisionBox(uint8_t blockId) {
        static const AABB empty(glm::vec3(0.0f), glm::vec3(0.0f));
        if (blockId == 0) return empty;

        const Block* block = Registry<Block>::getRegistry().get(blockId);
        if (!block) return empty;

        const AABB& box = block->getCollisionBox();
        return box.isEmpty() ? empty : box;
    }

    AABB CollisionSystem::blockAABBAt(uint8_t blockId, int x, int y, int z) {
        const AABB& box = getBlockCollisionBox(blockId);
        if (box.isEmpty()) return box;

        const glm::vec3 base(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        return AABB(base + box.min, base + box.max);
    }

    AABB CollisionSystem::blockAABBAt(const Block& block, int x, int y, int z) {
        return blockAABBAt(static_cast<uint8_t>(block.getId()), x, y, z);
    }

    std::optional<AABB> CollisionSystem::getWorldBlockAABB(const World& world, int x, int y, int z) {
        if (y < 0 || y >= world.getHeight()) return std::nullopt;

        const uint8_t blockId = world.getBlock(x, y, z);
        if (blockId == 0) return std::nullopt;

        const AABB& box = getBlockCollisionBox(blockId);
        if (box.isEmpty()) return std::nullopt;

        const glm::vec3 base(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        return AABB(base + box.min, base + box.max);
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
                for (int x = x0; x <= x1; ++x) {
                    auto cellBox = getWorldBlockAABB(world, x, y, z);
                    if (cellBox && box.overlaps(*cellBox)) return true;
                }

        return false;
    }

    void CollisionSystem::moveEntity(const World& world, AABB& box, glm::vec3& velocity, float dt) {
        const glm::vec3 delta = velocity * dt;
        const glm::vec3 size = box.getSize();

        auto resolveAxis = [&](int axis, float deltaAxis) {
            if (deltaAxis == 0.0f) return;

            glm::vec3 shift(0.0f);
            shift[axis] = deltaAxis;
            box = box.translate(shift);

            int x0 = static_cast<int>(std::floor(box.min.x));
            int x1 = static_cast<int>(std::ceil(box.max.x)) - 1;
            int y0 = static_cast<int>(std::floor(box.min.y));
            int y1 = static_cast<int>(std::ceil(box.max.y)) - 1;
            int z0 = static_cast<int>(std::floor(box.min.z));
            int z1 = static_cast<int>(std::ceil(box.max.z)) - 1;

            float face = 0.0f;
            bool hit = false;

            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z)
                    for (int x = x0; x <= x1; ++x) {
                        auto cellBox = getWorldBlockAABB(world, x, y, z);
                        if (!cellBox || !box.overlaps(*cellBox)) continue;

                        if (deltaAxis > 0.0f) {
                            float f = cellBox->min[axis];
                            if (f < box.min[axis]) continue;
                            if (!hit || f < face) face = f;
                        } else {
                            float f = cellBox->max[axis];
                            if (f > box.max[axis]) continue;
                            if (!hit || f > face) face = f;
                        }
                        hit = true;
                    }

            if (hit) {
                if (deltaAxis > 0.0f) {
                    box.max[axis] = face;
                    box.min[axis] = box.max[axis] - size[axis];
                } else {
                    box.min[axis] = face;
                    box.max[axis] = box.min[axis] + size[axis];
                }
                velocity[axis] = 0.0f;
            }
        };

        resolveAxis(0, delta.x);
        resolveAxis(2, delta.z);
        resolveAxis(1, delta.y);
    }

} // namespace lve
