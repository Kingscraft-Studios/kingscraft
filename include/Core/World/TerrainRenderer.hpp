#pragma once

#include "Core/World/Chunk.hpp"
#include <memory>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

#include "Renderer/FrameScene.hpp"

namespace kc {

    using ChunkLookupFn = const Chunk* (*)(int gridX, int gridZ, void* context);

    class TerrainRenderer {
    public:
        TerrainRenderer() = default;

        void render(FrameScene& scene, const std::vector<Chunk*>& chunks, const glm::mat4& viewProj, const glm::vec3& cameraPos,
                    bool enableFrustumCulling, float worldHeight,
                    ChunkLookupFn lookupFn = nullptr,
                    void* lookupContext = nullptr,
                    double* outFrustumMs = nullptr,
                    double* outDrawMs = nullptr,
                    uint32_t* outVisibleChunks = nullptr,
                    uint32_t* outVisibleSubChunks = nullptr,
                    uint32_t* outOcclusionTested = nullptr,
                    uint32_t* outOcclusionRemoved = nullptr);

    private:

        void computeFrustumVisibleChunks(
            const glm::vec3& cameraPos, const glm::mat4& viewProj,
            float cs, ChunkLookupFn lookupFn, void* lookupContext,
            const std::vector<Chunk*>& chunks, uint64_t mutationGen);

        bool lastDisableTextures_ = false;

        // Cache inverse viewProj — only recompute when viewProj changes
        glm::mat4 cachedViewProj_{};
        glm::mat4 cachedInvVP_{};
        bool invVPCacheValid_ = false;

        // Per-cell far-plane world-space targets, derived purely from the
        // cached inverse viewProj. Rebuilt only when viewProj or grid size
        // changes (camera-independent), so a full ray pass does no matrix work.
        std::vector<glm::vec3> farPoints_;
        int farPointsW_ = 0;
        int farPointsH_ = 0;
        glm::mat4 farProj_{};

        // Dense column grid around the camera. Slot =
        // (gx - camGx + r) + (gz - camGz + r) * side,  side = 2*r+1.
        // Rebuilt per frame from the loaded-chunk list (no hash lookups) and
        // null for unloaded columns (non-blocking, same as a missing lookup).
        std::vector<const Chunk*> colGrid_;
        std::vector<uint16_t> reachStamp_;
        uint16_t reachEpoch_ = 0;
        int gridSide_ = 0;      // 2*r+1 (0 = not built yet)
        int gridR_ = 0;         // coverage radius, in columns

        // Temporal cache: the reach stamps are only valid for the exact inputs
        // that produced them. Reuse the previous frame's stamps when the camera
        // position, view-projection and world block-data fingerprint are all
        // unchanged — the result is then provably identical.
        glm::vec3 lastOcclCamPos_{};
        uint64_t lastMutationGen_ = 0;
        size_t lastChunkCount_ = 0;
        bool occlCacheValid_ = false;

        // Reused frame buffers to avoid per-frame heap churn
        std::vector<Chunk*> visibleBuf_;
        std::vector<Chunk*> filteredBuf_;
    };

} // namespace kc