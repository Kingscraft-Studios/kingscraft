#include "Core/World/ChunkMesher.hpp"
#include "Core/Blocks/Block.hpp"
#include "Core/Registry.hpp"
#include "Core/Resources/BlockModel.hpp"
#include <glm/glm.hpp>
#include <algorithm>

namespace kc {

namespace {
    // Face direction metadata
    // dir: 0=PosY, 1=NegY, 2=PosZ, 3=NegZ, 4=PosX, 5=NegX
    constexpr int normAxis[6] = {1, 1, 2, 2, 0, 0};
    constexpr int normSign[6] = {1, -1, 1, -1, 1, -1};
    constexpr int uAxis[6] = {0, 0, 0, 0, 2, 2};
    constexpr int vAxis[6] = {2, 2, 1, 1, 1, 1};

    constexpr int nx[6] = { 0,  0,  0,  0,  1, -1};
    constexpr int ny[6] = { 1, -1,  0,  0,  0,  0};
    constexpr int nz[6] = { 0,  0,  1, -1,  0,  0};

    constexpr FaceDir faceDirs[6] = {
        FaceDir::PosY, FaceDir::NegY,
        FaceDir::PosZ, FaceDir::NegZ,
        FaceDir::PosX, FaceDir::NegX
    };

    constexpr glm::vec3 axisVec[3] = {
        glm::vec3(1, 0, 0),
        glm::vec3(0, 1, 0),
        glm::vec3(0, 0, 1)
    };

    // Precomputed: true when cross(uAxis, vAxis) dot outward > 0
    // (triangle would be CCW from outside → back face → needs reversal)
    constexpr bool reverseWinding[6] = {
        false,  // dir 0 (PosY): cross(X, Z) = -Y, outward = +Y, dot < 0 → CW → keep
        true,   // dir 1 (NegY): cross(X, Z) = -Y, outward = -Y, dot > 0 → CCW → flip
        true,   // dir 2 (PosZ): cross(X, Y) = +Z, outward = +Z, dot > 0 → CCW → flip
        false,  // dir 3 (NegZ): cross(X, Y) = +Z, outward = -Z, dot < 0 → CW → keep
        false,  // dir 4 (PosX): cross(Z, Y) = -X, outward = +X, dot < 0 → CW → keep
        true    // dir 5 (NegX): cross(Z, Y) = -X, outward = -X, dot > 0 → CCW → flip
    };
}

void ChunkMesher::generateSubChunk(
    SubChunk& subChunk,
    const std::vector<uint8_t>& blockIds,
    int N, int height,
    int yBase,
    const std::vector<uint8_t>* edgePosX,
    const std::vector<uint8_t>* edgeNegX,
    const std::vector<uint8_t>* edgePosZ,
    const std::vector<uint8_t>* edgeNegZ)
{
    auto& vertices = subChunk.vertices;
    auto& indices = subChunk.indices;
    vertices.clear();
    indices.clear();
    subChunk.indexCount = 0;

    int yEnd = std::min(yBase + static_cast<int>(SUBCHUNK_H), height);
    if (yBase >= height) return;

    auto& registry = Registry<Block>::getRegistry();

    auto getBlock = [&](int x, int y, int z) -> uint8_t {
        if (y < 0 || y >= height)
            return 0;
        if (x < 0) {
            if (edgeNegX) return (*edgeNegX)[static_cast<size_t>(y) * N + z];
            return 0;
        }
        if (x >= N) {
            if (edgePosX) return (*edgePosX)[static_cast<size_t>(y) * N + z];
            return 0;
        }
        if (z < 0) {
            if (edgeNegZ) return (*edgeNegZ)[static_cast<size_t>(y) * N + x];
            return 0;
        }
        if (z >= N) {
            if (edgePosZ) return (*edgePosZ)[static_cast<size_t>(y) * N + x];
            return 0;
        }
        return blockIds[static_cast<size_t>(y) * N * N + z * N + x];
    };

    for (int dir = 0; dir < 6; ++dir) {
        int na = normAxis[dir];
        int ns = normSign[dir];
        int ua = uAxis[dir];
        int va = vAxis[dir];

        int depthStart = (na == 1) ? yBase : 0;
        int depthEnd   = (na == 1) ? yEnd : N;
        int uDim       = (ua == 1) ? (yEnd - yBase) : N;
        int vDim       = (va == 1) ? (yEnd - yBase) : N;

        for (int depth = depthStart; depth < depthEnd; ++depth) {
            size_t sliceSize = static_cast<size_t>(uDim) * vDim;
            std::vector<bool> mask(sliceSize, false);
            std::vector<uint16_t> texIdx(sliceSize, 0);

            for (int v = 0; v < vDim; ++v) {
                for (int u = 0; u < uDim; ++u) {
                    int coords[3] = {};
                    coords[na] = depth;
                    coords[ua] = u;
                    coords[va] = v;

                    if (ua == 1) coords[1] += yBase;
                    if (va == 1) coords[1] += yBase;

                    uint8_t blockId = getBlock(coords[0], coords[1], coords[2]);
                    if (blockId == 0) continue;

                    {
                        int nc[3] = {coords[0] + nx[dir], coords[1] + ny[dir], coords[2] + nz[dir]};
                        if (getBlock(nc[0], nc[1], nc[2]) != 0) continue;
                    }

                    Block* block = registry.get(blockId);
                    if (!block) continue;

                    const Quad* quad = block->getModel().findQuad(faceDirs[dir]);
                    if (!quad) continue;

                    uint16_t gti = static_cast<uint16_t>(block->getTextureBaseOffset() + quad->tileIndex);
                    int idx = v * uDim + u;
                    mask[idx] = true;
                    texIdx[idx] = gti;
                }
            }

            std::vector<bool> visited(sliceSize, false);

            for (int v = 0; v < vDim; ++v) {
                for (int u = 0; u < uDim; ++u) {
                    int idx = v * uDim + u;
                    if (!mask[idx] || visited[idx]) continue;

                    uint16_t cellTex = texIdx[idx];

                    int rectW = 1;
                    while (u + rectW < uDim) {
                        int ri = v * uDim + (u + rectW);
                        if (!mask[ri] || visited[ri] || texIdx[ri] != cellTex) break;
                        rectW++;
                    }

                    int rectH = 1;
                    bool canExpand = true;
                    while (v + rectH < vDim && canExpand) {
                        for (int u2 = u; u2 < u + rectW; ++u2) {
                            int ri = (v + rectH) * uDim + u2;
                            if (!mask[ri] || visited[ri] || texIdx[ri] != cellTex) {
                                canExpand = false;
                                break;
                            }
                        }
                        if (canExpand) rectH++;
                    }

                    for (int dv = 0; dv < rectH; ++dv) {
                        for (int du = 0; du < rectW; ++du) {
                            visited[(v + dv) * uDim + (u + du)] = true;
                        }
                    }

                    int basePos[3] = {};
                    basePos[na] = depth + (ns == 1 ? 1 : 0);
                    basePos[ua] = u;
                    basePos[va] = v;

                    if (na != 1) basePos[1] += yBase;

                    glm::vec3 baseLocal(
                        static_cast<float>(basePos[0]),
                        static_cast<float>(basePos[1]),
                        static_cast<float>(basePos[2])
                    );

                    glm::vec3 du = axisVec[ua] * static_cast<float>(rectW);
                    glm::vec3 dv = axisVec[va] * static_cast<float>(rectH);

                    glm::vec3 localCorners[4] = {
                        baseLocal,
                        baseLocal + du,
                        baseLocal + du + dv,
                        baseLocal + dv
                    };

                    int8_t uvs[4][2] = {
                        {0, static_cast<int8_t>(rectH)},
                        {static_cast<int8_t>(rectW), static_cast<int8_t>(rectH)},
                        {static_cast<int8_t>(rectW), 0},
                        {0, 0}
                    };

                    int baseVertex = static_cast<int>(vertices.size());

                    for (int vi = 0; vi < 4; ++vi) {
                        vertices.push_back({
                            static_cast<uint8_t>(localCorners[vi].x),
                            static_cast<uint8_t>(localCorners[vi].y),
                            static_cast<uint8_t>(localCorners[vi].z),
                            static_cast<uint8_t>(dir),
                            uvs[vi][0],
                            uvs[vi][1],
                            cellTex
                        });
                    }

                    if (reverseWinding[dir]) {
                        indices.push_back(static_cast<uint16_t>(baseVertex));
                        indices.push_back(static_cast<uint16_t>(baseVertex + 2));
                        indices.push_back(static_cast<uint16_t>(baseVertex + 1));
                        indices.push_back(static_cast<uint16_t>(baseVertex));
                        indices.push_back(static_cast<uint16_t>(baseVertex + 3));
                        indices.push_back(static_cast<uint16_t>(baseVertex + 2));
                    } else {
                        indices.push_back(static_cast<uint16_t>(baseVertex));
                        indices.push_back(static_cast<uint16_t>(baseVertex + 1));
                        indices.push_back(static_cast<uint16_t>(baseVertex + 2));
                        indices.push_back(static_cast<uint16_t>(baseVertex));
                        indices.push_back(static_cast<uint16_t>(baseVertex + 2));
                        indices.push_back(static_cast<uint16_t>(baseVertex + 3));
                    }
                }
            }
        }
    }

    subChunk.indexCount = static_cast<uint32_t>(indices.size());
}

void ChunkMesher::emitGateFaces(
    SubChunk& subChunk,
    const std::vector<uint8_t>& blockIds,
    int N, int height,
    int yBase,
    int gate,
    const std::vector<uint8_t>* edgePosX,
    const std::vector<uint8_t>* edgeNegX,
    const std::vector<uint8_t>* edgePosZ,
    const std::vector<uint8_t>* edgeNegZ)
{
    if (gate < 2 || gate > 5) return;

    auto& vertices = subChunk.vertices;
    auto& indices = subChunk.indices;

    int yEnd = std::min(yBase + static_cast<int>(SUBCHUNK_H), height);
    if (yBase >= height) return;

    auto& registry = Registry<Block>::getRegistry();

    auto getBlock = [&](int x, int y, int z) -> uint8_t {
        if (y < 0 || y >= height)
            return 0;
        if (x < 0) {
            if (edgeNegX) return (*edgeNegX)[static_cast<size_t>(y) * N + z];
            return 0;
        }
        if (x >= N) {
            if (edgePosX) return (*edgePosX)[static_cast<size_t>(y) * N + z];
            return 0;
        }
        if (z < 0) {
            if (edgeNegZ) return (*edgeNegZ)[static_cast<size_t>(y) * N + x];
            return 0;
        }
        if (z >= N) {
            if (edgePosZ) return (*edgePosZ)[static_cast<size_t>(y) * N + x];
            return 0;
        }
        return blockIds[static_cast<size_t>(y) * N * N + z * N + x];
    };

    int na = normAxis[gate];
    int ns = normSign[gate];
    int ua = uAxis[gate];
    int va = vAxis[gate];

    int uDim = (ua == 1) ? (yEnd - yBase) : N;
    int vDim = (va == 1) ? (yEnd - yBase) : N;
    int depth = (ns == 1) ? (N - 1) : 0;

    size_t sliceSize = static_cast<size_t>(uDim) * vDim;
    std::vector<bool> mask(sliceSize, false);
    std::vector<uint16_t> texIdx(sliceSize, 0);

    for (int v = 0; v < vDim; ++v) {
        for (int u = 0; u < uDim; ++u) {
            int coords[3] = {};
            coords[na] = depth;
            coords[ua] = u;
            coords[va] = v;

            if (ua == 1) coords[1] += yBase;
            if (va == 1) coords[1] += yBase;

            uint8_t blockId = getBlock(coords[0], coords[1], coords[2]);
            if (blockId == 0) continue;

            {
                int nc[3] = {coords[0] + nx[gate], coords[1] + ny[gate], coords[2] + nz[gate]};
                if (getBlock(nc[0], nc[1], nc[2]) != 0) continue;
            }

            Block* block = registry.get(blockId);
            if (!block) continue;

            const Quad* quad = block->getModel().findQuad(faceDirs[gate]);
            if (!quad) continue;

            int idx = v * uDim + u;
            mask[idx] = true;
            texIdx[idx] = static_cast<uint16_t>(block->getTextureBaseOffset() + quad->tileIndex);
        }
    }

    std::vector<bool> visited(sliceSize, false);

    for (int v = 0; v < vDim; ++v) {
        for (int u = 0; u < uDim; ++u) {
            int idx = v * uDim + u;
            if (!mask[idx] || visited[idx]) continue;

            uint16_t cellTex = texIdx[idx];

            int rectW = 1;
            while (u + rectW < uDim) {
                int ri = v * uDim + (u + rectW);
                if (!mask[ri] || visited[ri] || texIdx[ri] != cellTex) break;
                rectW++;
            }

            int rectH = 1;
            bool canExpand = true;
            while (v + rectH < vDim && canExpand) {
                for (int u2 = u; u2 < u + rectW; ++u2) {
                    int ri = (v + rectH) * uDim + u2;
                    if (!mask[ri] || visited[ri] || texIdx[ri] != cellTex) {
                        canExpand = false;
                        break;
                    }
                }
                if (canExpand) rectH++;
            }

            for (int dv = 0; dv < rectH; ++dv) {
                for (int du = 0; du < rectW; ++du) {
                    visited[(v + dv) * uDim + (u + du)] = true;
                }
            }

            int basePos[3] = {};
            basePos[na] = depth + (ns == 1 ? 1 : 0);
            basePos[ua] = u;
            basePos[va] = v;

            if (na != 1) basePos[1] += yBase;

            glm::vec3 baseLocal(
                static_cast<float>(basePos[0]),
                static_cast<float>(basePos[1]),
                static_cast<float>(basePos[2])
            );

            glm::vec3 du = axisVec[ua] * static_cast<float>(rectW);
            glm::vec3 dv = axisVec[va] * static_cast<float>(rectH);

            glm::vec3 localCorners[4] = {
                baseLocal,
                baseLocal + du,
                baseLocal + du + dv,
                baseLocal + dv
            };

            int8_t uvs[4][2] = {
                {0, static_cast<int8_t>(rectH)},
                {static_cast<int8_t>(rectW), static_cast<int8_t>(rectH)},
                {static_cast<int8_t>(rectW), 0},
                {0, 0}
            };

            int baseVertex = static_cast<int>(vertices.size());

            for (int vi = 0; vi < 4; ++vi) {
                vertices.push_back({
                    static_cast<uint8_t>(localCorners[vi].x),
                    static_cast<uint8_t>(localCorners[vi].y),
                    static_cast<uint8_t>(localCorners[vi].z),
                    static_cast<uint8_t>(gate),
                    uvs[vi][0],
                    uvs[vi][1],
                    cellTex
                });
            }

            if (reverseWinding[gate]) {
                indices.push_back(static_cast<uint16_t>(baseVertex));
                indices.push_back(static_cast<uint16_t>(baseVertex + 2));
                indices.push_back(static_cast<uint16_t>(baseVertex + 1));
                indices.push_back(static_cast<uint16_t>(baseVertex));
                indices.push_back(static_cast<uint16_t>(baseVertex + 3));
                indices.push_back(static_cast<uint16_t>(baseVertex + 2));
            } else {
                indices.push_back(static_cast<uint16_t>(baseVertex));
                indices.push_back(static_cast<uint16_t>(baseVertex + 1));
                indices.push_back(static_cast<uint16_t>(baseVertex + 2));
                indices.push_back(static_cast<uint16_t>(baseVertex));
                indices.push_back(static_cast<uint16_t>(baseVertex + 2));
                indices.push_back(static_cast<uint16_t>(baseVertex + 3));
            }
        }
    }

    subChunk.indexCount = static_cast<uint32_t>(indices.size());
}

} // namespace kc
