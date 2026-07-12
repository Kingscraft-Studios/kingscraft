#pragma once

#include "Core/FrameContext.hpp"
#include "Core/World/Chunk.hpp"
#include "Vulkan/Pipeline.hpp"
#include <memory>
#include <vector>
#include <unordered_set>
#include <glm/glm.hpp>

namespace lve {

    class Device;
    class TextureCache;

    using ChunkLookupFn = const Chunk* (*)(int gridX, int gridZ, void* context);

    class TerrainRenderer {
    public:
        TerrainRenderer() = default;
        ~TerrainRenderer();

        void init(Device& device, TextureCache& textureCache, VkRenderPass renderPass);
        void cleanup();

        void render(VkCommandBuffer cmd, const std::vector<Chunk*>& chunks,
                    const glm::mat4& viewProj, const glm::vec3& cameraPos,
                    bool enableFrustumCulling, float worldHeight,
                    ChunkLookupFn lookupFn = nullptr,
                    void* lookupContext = nullptr,
                    double* outFrustumMs = nullptr,
                    double* outDrawMs = nullptr,
                    uint32_t* outVisibleChunks = nullptr,
                    uint32_t* outVisibleSubChunks = nullptr,
                    uint32_t* outOcclusionTested = nullptr,
                    uint32_t* outOcclusionRemoved = nullptr);

        void onRenderPassChanged(VkRenderPass renderPass);

    private:
        void createPipelineLayout(TextureCache& textureCache);
        void createPipeline(bool disableTextures);

        void computeFrustumVisibleChunks(
            const glm::vec3& cameraPos, const glm::mat4& viewProj,
            float cs, ChunkLookupFn lookupFn, void* lookupContext);

        Device* device_ = nullptr;
        TextureCache* textureCache_ = nullptr;
        VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
        VkRenderPass renderPass_ = VK_NULL_HANDLE;
        std::unique_ptr<Pipeline> pipeline_;
        bool lastDisableTextures_ = false;

        // Occlusion: reused across frames to avoid per-frame allocation
        std::unordered_set<uint64_t> reachedSet_;

        // Cache inverse viewProj — only recompute when viewProj changes
        glm::mat4 cachedViewProj_{};
        glm::mat4 cachedInvVP_{};
        bool invVPCacheValid_ = false;
    };

} // namespace lve
