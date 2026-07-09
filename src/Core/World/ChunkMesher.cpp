#include "Core/World/ChunkMesher.hpp"
#include "Core/Blocks/Block.hpp"
#include "Core/Registry.hpp"
#include "Core/Resources/BlockModel.hpp"
#include <glm/glm.hpp>

namespace lve {

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

    const Quad* findQuad(const BlockModel& model, FaceDir dir) {
        for (const auto& elem : model.getElements()) {
            for (const auto& q : elem.quads) {
                if (q.face == dir) return &q;
            }
        }
        return nullptr;
    }
}

void ChunkMesher::generate(Chunk& chunk, const std::vector<uint8_t>& blockIds, int height,
                           const std::vector<uint8_t>* edgePosX,
                           const std::vector<uint8_t>* edgeNegX,
                           const std::vector<uint8_t>* edgePosZ,
                           const std::vector<uint8_t>* edgeNegZ) {
    int N = chunk.getVerticesPerAxis();
    glm::vec3 origin = chunk.getWorldOrigin();

    auto& vertices = chunk.vertices();
    auto& indices = chunk.indices();
    vertices.clear();
    indices.clear();

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

        int depthDim = (na == 1) ? height : N;
        int uDim = (ua == 1) ? height : N;
        int vDim = (va == 1) ? height : N;

        for (int depth = 0; depth < depthDim; ++depth) {
            size_t sliceSize = static_cast<size_t>(uDim) * vDim;
            std::vector<bool> mask(sliceSize, false);
            std::vector<float> texIdx(sliceSize, -1.0f);

            for (int v = 0; v < vDim; ++v) {
                for (int u = 0; u < uDim; ++u) {
                    int coords[3] = {};
                    coords[na] = depth;
                    coords[ua] = u;
                    coords[va] = v;

                    uint8_t blockId = getBlock(coords[0], coords[1], coords[2]);
                    if (blockId == 0) continue;

                    {
                        int nc[3] = {coords[0] + nx[dir], coords[1] + ny[dir], coords[2] + nz[dir]};
                        if (getBlock(nc[0], nc[1], nc[2]) != 0) continue;
                    }

                    Block* block = registry.get(blockId);
                    if (!block) continue;

                    const Quad* quad = findQuad(block->getModel(), faceDirs[dir]);
                    if (!quad) continue;

                    float gti = static_cast<float>(block->getTextureBaseOffset() + quad->tileIndex);
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

                    float cellTex = texIdx[idx];

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

                    glm::vec3 baseWorld(
                        origin.x + basePos[0],
                        origin.y + basePos[1],
                        origin.z + basePos[2]
                    );

                    glm::vec3 du = axisVec[ua] * static_cast<float>(rectW);
                    glm::vec3 dv = axisVec[va] * static_cast<float>(rectH);

                    int baseVertex = static_cast<int>(vertices.size());

                    glm::vec3 corners[4] = {
                        baseWorld,
                        baseWorld + du,
                        baseWorld + du + dv,
                        baseWorld + dv
                    };

                    glm::vec2 uvs[4] = {
                        {0.0f, static_cast<float>(rectH)},
                        {static_cast<float>(rectW), static_cast<float>(rectH)},
                        {static_cast<float>(rectW), 0.0f},
                        {0.0f, 0.0f}
                    };

                    for (int vi = 0; vi < 4; ++vi) {
                        vertices.push_back({corners[vi], uvs[vi], cellTex});
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
}

} // namespace lve
