#pragma once

#include "Core/World/Chunk.hpp"
#include <memory>
#include <vector>
#include <unordered_set>
#include <glm/glm.hpp>

#include "Renderer/FrameScene.hpp"

namespace lve {

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
            float cs, ChunkLookupFn lookupFn, void* lookupContext);
        bool lastDisableTextures_ = false;

        // Occlusion: reused across frames to avoid per-frame allocation
        std::unordered_set<uint64_t> reachedSet_;

        // Cache inverse viewProj — only recompute when viewProj changes
        glm::mat4 cachedViewProj_{};
        glm::mat4 cachedInvVP_{};
        bool invVPCacheValid_ = false;
    };

} // namespace lve
