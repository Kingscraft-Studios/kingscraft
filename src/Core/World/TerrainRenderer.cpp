#include "Core/World/TerrainRenderer.hpp"
#include "Core/World/ChunkKey.hpp"
#include "Core/World/World.hpp"
#include "Core/Frustum.hpp"
#include "Vulkan/TextureCache.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Util/TimeUtil.hpp"
#include "../../../include/Core/Bootstrapper.hpp"
#include "Core/World/Chunk.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kc {

    static constexpr float FRUSTUM_MARGIN = 1.0f;
    static constexpr float OCCLUSION_EPSILON = 1.5f;
    static constexpr float OCCLUSION_NEAR_FACTOR = 4.0f;
    static constexpr float DDA_SAMPLE_OFFSET = 0.001f;
    static constexpr int OCCLUSION_GRID_SLACK = 2;   // extra columns past farPlane

    // DDA traversal of a single ray through chunk columns on the XZ plane.
    // Marks every column the ray passes through as "reached".
    // Stops when terrain heightmap blocks the ray.
    //
    // Terrain-only occlusion.
    // Replace with voxel DDA if world gains overhangs.
    //
    // Identical algorithm/thresholds as before, but the reached marks land in a
    // dense epoch-stamped grid instead of a hash set, and the blocking chunk is
    // resolved from that grid (no per-step hash lookups).
    static void traceRay(
        const glm::vec3& origin, const glm::vec3& dir,
        float maxDist, float cs, float epsilon,
        int camGx, int camGz, int side, int r,
        const std::vector<const Chunk*>& colGrid,
        std::vector<uint16_t>& reachStamp, uint16_t epoch) {
        int gx = static_cast<int>(std::floor(origin.x / cs));
        int gz = static_cast<int>(std::floor(origin.z / cs));

        int stepX = (dir.x >= 0.0f) ? 1 : -1;
        int stepZ = (dir.z >= 0.0f) ? 1 : -1;

        float tMaxX, tMaxZ, tDeltaX, tDeltaZ;

        if (std::abs(dir.x) > 1e-8f) {
            float invDirX = 1.0f / dir.x;
            float nextBoundaryX = (dir.x > 0.0f)
                ? static_cast<float>(gx + 1) * cs - origin.x
                : origin.x - static_cast<float>(gx) * cs;
            tMaxX = nextBoundaryX * std::abs(invDirX);
            tDeltaX = cs * std::abs(invDirX);
        } else {
            tMaxX = std::numeric_limits<float>::max();
            tDeltaX = std::numeric_limits<float>::max();
        }

        if (std::abs(dir.z) > 1e-8f) {
            float invDirZ = 1.0f / dir.z;
            float nextBoundaryZ = (dir.z > 0.0f)
                ? static_cast<float>(gz + 1) * cs - origin.z
                : origin.z - static_cast<float>(gz) * cs;
            tMaxZ = nextBoundaryZ * std::abs(invDirZ);
            tDeltaZ = cs * std::abs(invDirZ);
        } else {
            tMaxZ = std::numeric_limits<float>::max();
            tDeltaZ = std::numeric_limits<float>::max();
        }

        float t = 0.0f;

        while (t < maxDist) {
            // Mark this column as reached BEFORE checking blocking.
            // The blocking chunk itself must be visible (it's the wall you see).
            int dx = gx - camGx + r;
            int dz = gz - camGz + r;
            if (dx >= 0 && dx < side && dz >= 0 && dz < side) {
                size_t idx = static_cast<size_t>(dz) * static_cast<size_t>(side) + static_cast<size_t>(dx);
                reachStamp[idx] = epoch;

                // Check heightmap at this column
                const Chunk* chunk = colGrid[idx];
                if (chunk && chunk->getMaxHeight() > 0) {
                    // Sample slightly inside the cell to avoid boundary precision issues
                    float sampleT = t + DDA_SAMPLE_OFFSET;
                    float localX = origin.x + dir.x * sampleT - chunk->getWorldOrigin().x;
                    float localZ = origin.z + dir.z * sampleT - chunk->getWorldOrigin().z;
                    int lx = std::clamp(static_cast<int>(localX), 0, static_cast<int>(cs) - 1);
                    int lz = std::clamp(static_cast<int>(localZ), 0, static_cast<int>(cs) - 1);

                    float rayY = origin.y + dir.y * sampleT;
                    if (static_cast<float>(chunk->getHeightAt(lx, lz)) > rayY + epsilon)
                        return;  // BLOCKED
                }
            }

            // Step to next column boundary
            if (tMaxX < tMaxZ) {
                t = tMaxX;
                tMaxX += tDeltaX;
                gx += stepX;
            } else {
                t = tMaxZ;
                tMaxZ += tDeltaZ;
                gz += stepZ;
            }
        }
    }


    void TerrainRenderer::computeFrustumVisibleChunks(
        const glm::vec3& cameraPos, const glm::mat4& viewProj,
        float cs, ChunkLookupFn lookupFn, void* lookupContext,
        const std::vector<Chunk*>& chunks, uint64_t mutationGen) {
        auto& settings = RendererSettings::get();

        // Cache inverse viewProj — only recompute when viewProj changes.
        bool viewProjChanged = (!invVPCacheValid_ || viewProj != cachedViewProj_);
        if (viewProjChanged) {
            cachedInvVP_ = glm::inverse(viewProj);
            cachedViewProj_ = viewProj;
            invVPCacheValid_ = true;
        }

        // Temporal cache: with identical camera position, view-projection and
        // world data, a recompute would reproduce the current reach stamps
        // exactly, so skip the ray pass and keep the previous frame's result.
        if (occlCacheValid_ && !viewProjChanged &&
            cameraPos == lastOcclCamPos_ &&
            mutationGen == lastMutationGen_ &&
            chunks.size() == lastChunkCount_) {
            return;
        }

        int camGx = static_cast<int>(std::floor(cameraPos.x / cs));
        int camGz = static_cast<int>(std::floor(cameraPos.z / cs));

        // Underground detection.
        // Note: this tests "below highest terrain", not truly underground.
        // Indoor/cave scenarios may need refinement.
        const Chunk* camChunk = lookupFn ? lookupFn(camGx, camGz, lookupContext) : nullptr;
        bool underground = false;
        if (camChunk) {
            int lx = static_cast<int>(cameraPos.x - camChunk->getWorldOrigin().x);
            int lz = static_cast<int>(cameraPos.z - camChunk->getWorldOrigin().z);
            if (lx >= 0 && lx < static_cast<int>(cs) && lz >= 0 && lz < static_cast<int>(cs))
                if (cameraPos.y < static_cast<float>(camChunk->getHeightAt(lx, lz)) - 1.0f)
                    underground = true;
        }

        float maxDist = settings.farPlane;

        int gridW = settings.occlusionGridW;
        int gridH = settings.occlusionGridH;

        // Dense column grid covering the whole ray domain around the camera.
        int r = static_cast<int>(std::ceil(maxDist / cs)) + OCCLUSION_GRID_SLACK;
        int side = 2 * r + 1;
        size_t total = static_cast<size_t>(side) * side;
        if (gridSide_ != side) {
            colGrid_.assign(total, nullptr);
            reachStamp_.assign(total, 0);
            gridSide_ = side;
            gridR_ = r;
            reachEpoch_ = 0;
        }

        // Invalidate the previous frame's stamps: reach then only reflects the
        // markers written below (none when underground, matching prior behavior
        // where the reached set was emptied before the underground bail-out).
        if (++reachEpoch_ == 0) {
            std::fill(reachStamp_.begin(), reachStamp_.end(), 0);
            reachEpoch_ = 1;
        }

        if (underground) {
            lastOcclCamPos_ = cameraPos;
            lastMutationGen_ = mutationGen;
            lastChunkCount_ = chunks.size();
            occlCacheValid_ = true;
            return;     // Occlusion disabled underground — empty reach set
        }

        std::fill(colGrid_.begin(), colGrid_.end(), nullptr);
        for (Chunk* chunk : chunks) {
            if (!chunk) continue;
            glm::ivec2 gp = chunk->getGridPos();
            int dx = gp.x - camGx;
            int dz = gp.y - camGz;
            if (dx < -r || dx > r || dz < -r || dz > r) continue;
            colGrid_[static_cast<size_t>(dz + r) * side + static_cast<size_t>(dx + r)] = chunk;
        }

        // Per-cell far-plane targets — rebuilt when viewProj or grid size
        // changes, then reused for the whole pass (camera-independent; no
        // per-ray matrix math).
        if (farPointsW_ != gridW || farPointsH_ != gridH || viewProj != farProj_) {
            farPointsW_ = gridW;
            farPointsH_ = gridH;
            farProj_ = viewProj;
            farPoints_.resize(static_cast<size_t>(gridW) * gridH);
            for (int j = 0; j < gridH; ++j) {
                float v = (static_cast<float>(j) + 0.5f) / static_cast<float>(gridH);
                float clipY = 2.0f * v - 1.0f;

                for (int i = 0; i < gridW; ++i) {
                    float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(gridW);
                    float clipX = 2.0f * u - 1.0f;

                    glm::vec4 farWorld = cachedInvVP_ * glm::vec4(clipX, clipY, 1.0f, 1.0f);
                    if (farWorld.w != 0.0f)
                        farPoints_[static_cast<size_t>(j) * gridW + i] = glm::vec3(farWorld) / farWorld.w;
                    else
                        farPoints_[static_cast<size_t>(j) * gridW + i] = glm::vec3(0.0f);
                }
            }
        }

        for (int j = 0; j < gridH; ++j) {
            size_t rowBase = static_cast<size_t>(j) * gridW;
            for (int i = 0; i < gridW; ++i) {
                glm::vec3 target = farPoints_[rowBase + i];
                glm::vec3 dir = target - cameraPos;
                float dist = glm::length(dir);
                if (dist < 0.001f) continue;
                dir /= dist;

                // Clamp to far plane distance
                float rayDist = std::min(dist, maxDist);

                traceRay(cameraPos, dir, rayDist, cs, OCCLUSION_EPSILON,
                         camGx, camGz, side, r, colGrid_, reachStamp_, reachEpoch_);
            }
        }

        lastOcclCamPos_ = cameraPos;
        lastMutationGen_ = mutationGen;
        lastChunkCount_ = chunks.size();
        occlCacheValid_ = true;
    }

    void TerrainRenderer::render(FrameScene& scene, const std::vector<Chunk*>& chunks,
                                 const glm::mat4& viewProj, const glm::vec3& cameraPos,
                                 bool enableFrustumCulling, float worldHeight,
                                 ChunkLookupFn lookupFn, void* lookupContext,
                                  double* outFrustumMs, double* outDrawMs,
                                  uint32_t* outVisibleChunks,
                                  uint32_t* outVisibleSubChunks,
                                  uint32_t* outOcclusionTested,
                                  uint32_t* outOcclusionRemoved) {

        scene.camera.position = cameraPos;
        visibleBuf_.clear();
        visibleBuf_.reserve(chunks.size());
        std::vector<Chunk*>& visible = visibleBuf_;

        double frustumStart = TimeUtil::uptimeSeconds();

        uint32_t visibleSubCount = 0;

        if (enableFrustumCulling && !chunks.empty()) {
            float chunkSize = static_cast<float>(chunks[0]->getVerticesPerAxis() - 1);
            glm::vec3 halfExtents(chunkSize * 0.5f, worldHeight * 0.5f, chunkSize * 0.5f);

            Frustum frustum(viewProj);
            for (Chunk* chunk : chunks) {
                glm::vec3 origin = chunk->getWorldOrigin();
                glm::vec3 min = origin - glm::vec3(FRUSTUM_MARGIN, 0.0f, FRUSTUM_MARGIN);
                glm::vec3 max = origin + glm::vec3(chunkSize + FRUSTUM_MARGIN, worldHeight, chunkSize + FRUSTUM_MARGIN);
                if (!frustum.isVisible(min, max)) continue;
                if (chunk->getSubChunks().empty()) continue;
                visible.push_back(chunk);
                for (auto& sub : chunk->getSubChunks())
                    if (sub.indexCount > 0) visibleSubCount++;
            }

            std::sort(visible.begin(), visible.end(),
                [cameraPos, halfExtents](Chunk* a, Chunk* b) {
                    glm::vec3 da = (a->getWorldOrigin() + halfExtents) - cameraPos;
                    glm::vec3 db = (b->getWorldOrigin() + halfExtents) - cameraPos;
                    return da.x * da.x + da.y * da.y + da.z * da.z <
                           db.x * db.x + db.y * db.y + db.z * db.z;
                });
        } else {
            for (Chunk* chunk : chunks) {
                if (chunk->getSubChunks().empty()) continue;
                visible.push_back(chunk);
                for (auto& sub : chunk->getSubChunks())
                    if (sub.indexCount > 0) visibleSubCount++;
            }
        }

        // Occlusion culling (screen-space ray coverage with DDA traversal)
        uint32_t occlusionTested = 0;
        uint32_t occlusionRemoved = 0;
        if (lookupFn && enableFrustumCulling && RendererSettings::get().enableOcclusionCulling && !visible.empty()) {
            float cs = static_cast<float>(visible[0]->getVerticesPerAxis() - 1);
            float nearDist = cs * OCCLUSION_NEAR_FACTOR;

            uint64_t mutationGen = 0;
            if (lookupContext) {
                const World* world = static_cast<const World*>(lookupContext);
                mutationGen = world->getMutationGen();
            }

            computeFrustumVisibleChunks(cameraPos, viewProj, cs, lookupFn, lookupContext, chunks, mutationGen);

            int camGx = static_cast<int>(std::floor(cameraPos.x / cs));
            int camGz = static_cast<int>(std::floor(cameraPos.z / cs));

            filteredBuf_.clear();
            filteredBuf_.reserve(visible.size());
            for (Chunk* c : visible) {
                glm::vec3 d = (c->getWorldOrigin() + glm::vec3(cs * 0.5f, 0.0f, cs * 0.5f)) - cameraPos;
                if (d.x * d.x + d.y * d.y + d.z * d.z < nearDist * nearDist) {
                    filteredBuf_.push_back(c);
                    continue;
                }
                occlusionTested++;
                int cx = static_cast<int>(std::floor(c->getWorldOrigin().x / cs));
                int cz = static_cast<int>(std::floor(c->getWorldOrigin().z / cs));
                int dx = cx - camGx + gridR_;
                int dz = cz - camGz + gridR_;
                bool reached = (dx >= 0 && dx < gridSide_ && dz >= 0 && dz < gridSide_)
                    ? (reachStamp_[static_cast<size_t>(dz) * gridSide_ + static_cast<size_t>(dx)] == reachEpoch_)
                    : true;
                if (reached) {
                    filteredBuf_.push_back(c);
                } else {
                    occlusionRemoved++;
                }
            }
            visible.swap(filteredBuf_);
        }
        if (outOcclusionTested) *outOcclusionTested = occlusionTested;
        if (outOcclusionRemoved) *outOcclusionRemoved = occlusionRemoved;

        if (outFrustumMs)
            *outFrustumMs = (TimeUtil::uptimeSeconds() - frustumStart) * 1000.0;

        double drawStart = TimeUtil::uptimeSeconds();
        scene.terrain.draws.clear();
        scene.terrain.renderTerrain = !visible.empty();
        for (Chunk* c : visible) {
            scene.terrain.draws.push_back({
            makeChunkKey(c->getGridPos().x, c->getGridPos().y),
            c->getWorldOrigin()});
        }
        if (outDrawMs)
            *outDrawMs = (TimeUtil::uptimeSeconds() - drawStart) * 1000.0;

        if (outVisibleChunks)
            *outVisibleChunks = static_cast<uint32_t>(visible.size());
        if (outVisibleSubChunks)
            *outVisibleSubChunks = visibleSubCount;
        scene.ui.enabled = true;
    }

} // namespace kc
