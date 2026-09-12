#include "Core/World/Physics/CollisionSystem.hpp"

#include "Core/Blocks/Block.hpp"
#include "Core/Registry.hpp"
#include "Core/World/World.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace kc {

    bool CollisionSystem::isSolidBlock(const World& world, int x, int y, int z) {
        return getWorldBlockAABB(world, x, y, z).has_value();
    }

    const AABB& CollisionSystem::getBlockCollisionBox(uint64_t blockId) {
        static const AABB empty(glm::vec3(0.0f), glm::vec3(0.0f));
        if (blockId == 0) return empty;

        const Block* block = Registry<Block>::getRegistry().get(blockId);
        if (!block) return empty;

        const AABB& box = block->getCollisionBox();
        return box.isEmpty() ? empty : box;
    }

    AABB CollisionSystem::blockAABBAt(uint64_t blockId, int x, int y, int z) {
        const AABB& box = getBlockCollisionBox(blockId);
        if (box.isEmpty()) return box;

        const glm::vec3 base(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        return AABB(base + box.min, base + box.max);
    }

    AABB CollisionSystem::blockAABBAt(const Block& block, int x, int y, int z) {
        return blockAABBAt(block.getEncodedId(), x, y, z);
    }

    std::optional<AABB> CollisionSystem::getWorldBlockAABB(const World& world, int x, int y, int z) {
        if (y < 0 || y >= world.getHeight()) return std::nullopt;

        const uint64_t blockId = world.getBlock(x, y, z);
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

        auto resokcAxis = [&](int axis, float deltaAxis) {
            if (deltaAxis == 0.0f) return;

            const AABB before = box;
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
                        // The box was already inside this cell before the move:
                        // it is an embed (e.g. chunk popped in around the
                        // player), not a wall. Defer to depenetration below
                        // instead of ejecting the box the wrong way.
                        if (before.overlaps(*cellBox)) continue;

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

        // Depenetration: recover gracefully from embeds (a chunk loaded around
        // the player, or spawning inside terrain). Always rest upward first so
        // the player pops onto the surface instead of sliding into a neighbor
        // block; only slide horizontally if a ceiling blocks the upward escape.
        // Never push downward.
        auto depenetrate = [&]() {
            const auto maxF = std::numeric_limits<float>::max();
            for (int iter = 0; iter < 4; ++iter) {
                int x0 = static_cast<int>(std::floor(box.min.x));
                int x1 = static_cast<int>(std::ceil(box.max.x)) - 1;
                int y0 = static_cast<int>(std::floor(box.min.y));
                int y1 = static_cast<int>(std::ceil(box.max.y)) - 1;
                int z0 = static_cast<int>(std::floor(box.min.z));
                int z1 = static_cast<int>(std::ceil(box.max.z)) - 1;

                std::vector<AABB> hits;
                for (int y = y0; y <= y1; ++y)
                    for (int z = z0; z <= z1; ++z)
                        for (int x = x0; x <= x1; ++x) {
                            auto cellBox = getWorldBlockAABB(world, x, y, z);
                            if (cellBox && box.overlaps(*cellBox))
                                hits.push_back(*cellBox);
                        }
                if (hits.empty()) return true;

                // Preferred path: rest upward on the highest surface whose top
                // is inside the body, so a ground embed pops onto the surface.
                float bestTop = box.min.y;
                bool hasTop = false;
                for (auto const& c : hits) {
                    if (c.max.y < box.max.y && c.max.y > bestTop) {
                        bestTop = c.max.y;
                        hasTop = true;
                    }
                }
                if (hasTop) {
                    AABB cand = box;
                    cand.min.y = bestTop;
                    cand.max.y = bestTop + size.y;
                    if (!aabbCollides(world, cand)) {
                        box = cand;
                        if (velocity.y < 0.0f) velocity.y = 0.0f;
                        continue;
                    }
                }

                // Fallback: slide out along the least-penetration horizontal
                // axis. Use the max distance across ALL overlapping cells so a
                // single deterministic move clears every known overlap instead
                // of hopping between two blocks frame to frame.
                float needXp = 0.0f, needXm = 0.0f, needZp = 0.0f, needZm = 0.0f;
                for (auto const& c : hits) {
                    needXp = std::max(needXp, box.max.x - c.min.x);
                    needXm = std::max(needXm, c.max.x - box.min.x);
                    needZp = std::max(needZp, box.max.z - c.min.z);
                    needZm = std::max(needZm, c.max.z - box.min.z);
                }

                const glm::vec3 dirs[4] = {
                    {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}
                };
                const float needs[4] = {needXp, needXm, needZp, needZm};

                float bestLen = maxF;
                glm::vec3 bestDir(0.0f);
                for (int d = 0; d < 4; ++d) {   // fixed order = deterministic tie-break
                    if (needs[d] > 0.0f && needs[d] < bestLen) {
                        bestLen = needs[d];
                        bestDir = dirs[d];
                    }
                }

                if (bestLen >= maxF) return false;
                box = box.translate(bestDir * bestLen);
            }
            return !aabbCollides(world, box);
        };

        resokcAxis(0, delta.x);
        resokcAxis(2, delta.z);
        resokcAxis(1, delta.y);
        depenetrate();
    }

} // namespace kc
