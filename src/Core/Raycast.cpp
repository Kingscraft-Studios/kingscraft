#include "Core/Raycast.hpp"
#include "Core/World/World.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace lve {

    RaycastHit raycastBlock(const glm::vec3& origin, const glm::vec3& dir,
                            float maxDist, const World& world)
    {
        RaycastHit result;

        float dx = dir.x;
        float dy = dir.y;
        float dz = dir.z;

        int stepX = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
        int stepY = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
        int stepZ = (dz > 0) ? 1 : (dz < 0) ? -1 : 0;

        float tDeltaX = (stepX != 0) ? 1.0f / std::abs(dx) : std::numeric_limits<float>::max();
        float tDeltaY = (stepY != 0) ? 1.0f / std::abs(dy) : std::numeric_limits<float>::max();
        float tDeltaZ = (stepZ != 0) ? 1.0f / std::abs(dz) : std::numeric_limits<float>::max();

        int curX = static_cast<int>(std::floor(origin.x));
        int curY = static_cast<int>(std::floor(origin.y));
        int curZ = static_cast<int>(std::floor(origin.z));

        float tMaxX = (stepX > 0) ? (curX + 1.0f - origin.x) / dx
                    : (stepX < 0) ? (origin.x - curX) / -dx
                    : std::numeric_limits<float>::max();
        float tMaxY = (stepY > 0) ? (curY + 1.0f - origin.y) / dy
                    : (stepY < 0) ? (origin.y - curY) / -dy
                    : std::numeric_limits<float>::max();
        float tMaxZ = (stepZ > 0) ? (curZ + 1.0f - origin.z) / dz
                    : (stepZ < 0) ? (origin.z - curZ) / -dz
                    : std::numeric_limits<float>::max();

        int face = 0;
        float t = 0.0f;
        const int maxSteps = static_cast<int>(maxDist * 2.0f) + 16;

        for (int i = 0; i < maxSteps; ++i) {
            uint8_t block = world.getBlock(curX, curY, curZ);
            if (block != 0) {
                result.x = curX;
                result.y = curY;
                result.z = curZ;
                result.face = face;
                result.hit = true;
                return result;
            }

            if (tMaxX < tMaxY) {
                if (tMaxX < tMaxZ) {
                    curX += stepX;
                    t = tMaxX;
                    tMaxX += tDeltaX;
                    face = stepX > 0 ? 1 : 0;
                } else {
                    curZ += stepZ;
                    t = tMaxZ;
                    tMaxZ += tDeltaZ;
                    face = stepZ > 0 ? 5 : 4;
                }
            } else {
                if (tMaxY < tMaxZ) {
                    curY += stepY;
                    t = tMaxY;
                    tMaxY += tDeltaY;
                    face = stepY > 0 ? 3 : 2;
                } else {
                    curZ += stepZ;
                    t = tMaxZ;
                    tMaxZ += tDeltaZ;
                    face = stepZ > 0 ? 5 : 4;
                }
            }

            if (t > maxDist) break;
        }

        return result;
    }

}
