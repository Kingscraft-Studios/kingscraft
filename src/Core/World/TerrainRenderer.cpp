#include "Core/World/TerrainRenderer.hpp"
#include "Core/Frustum.hpp"
#include "Vulkan/TextureCache.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Util/TimeUtil.hpp"
#include "Util/Preloader.hpp"
#include "Core/World/Chunk.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lve {

    struct TerrainPushConstants {
        glm::mat4 viewProj;     // 64 bytes
        glm::vec4 chunkOrigin;  // 16 bytes
    };

    static constexpr float FRUSTUM_MARGIN = 1.0f;
    static constexpr float OCCLUSION_EPSILON = 1.5f;
    static constexpr float OCCLUSION_NEAR_FACTOR = 4.0f;
    static constexpr float DDA_SAMPLE_OFFSET = 0.001f;

    static uint64_t makeChunkKey(int x, int z) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
               static_cast<uint32_t>(z);
    }

    // DDA traversal of a single ray through chunk columns on the XZ plane.
    // Marks every column the ray passes through as "reached".
    // Stops when terrain heightmap blocks the ray.
    //
    // Terrain-only occlusion.
    // Replace with voxel DDA if world gains overhangs.
    static void traceRay(
        const glm::vec3& origin, const glm::vec3& dir,
        float maxDist, float cs, float epsilon,
        ChunkLookupFn lookupFn, void* lookupContext,
        std::unordered_set<uint64_t>& reached)
    {
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
            reached.insert(makeChunkKey(gx, gz));

            // Check heightmap at this column
            const Chunk* chunk = lookupFn(gx, gz, lookupContext);
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

    TerrainRenderer::~TerrainRenderer() {
        cleanup();
    }

    void TerrainRenderer::init(Device& device, TextureCache& textureCache, VkRenderPass renderPass) {
        device_ = &device;
        textureCache_ = &textureCache;
        renderPass_ = renderPass;
        createPipelineLayout(textureCache);
        lastDisableTextures_ = RendererSettings::get().disableTextures;
        createPipeline(lastDisableTextures_);
    }

    void TerrainRenderer::cleanup() {
        pipeline_.reset();
        if (pipelineLayout_ != VK_NULL_HANDLE && device_) {
            vkDestroyPipelineLayout(device_->device(), pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
    }

    void TerrainRenderer::createPipelineLayout(TextureCache& textureCache) {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(TerrainPushConstants);

        VkDescriptorSetLayout texLayout = textureCache.getLayout();

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;
        layoutInfo.setLayoutCount = (texLayout != VK_NULL_HANDLE) ? 1 : 0;
        layoutInfo.pSetLayouts = (texLayout != VK_NULL_HANDLE) ? &texLayout : nullptr;

        if (vkCreatePipelineLayout(device_->device(), &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }

    void TerrainRenderer::createPipeline(bool disableTextures) {
        auto& vertShaderCode = Preloader::Get().getShader("resources/shaders/terrain.vert.spv");
        auto& fragShaderCode = Preloader::Get().getShader("resources/shaders/terrain.frag.spv");

        PipelineConfigInfo configInfo{};
        Pipeline::defaultPipelineConfigInfo(configInfo);
        configInfo.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;

        VkSpecializationMapEntry entry{};
        entry.constantID = 0;
        entry.offset = 0;
        entry.size = sizeof(uint32_t);
        configInfo.specMapEntries = {entry};
        uint32_t specValue = disableTextures ? 1 : 0;
        configInfo.specData = {specValue};

        auto bindingDesc = ChunkVertex::getBindingDescription();
        auto attributeDescs = ChunkVertex::getAttributeDescriptions();
        configInfo.bindingDescriptions = {bindingDesc};
        configInfo.attributeDescriptions = {attributeDescs.begin(), attributeDescs.end()};

        configInfo.renderPass = renderPass_;
        configInfo.pipelineLayout = pipelineLayout_;

        pipeline_ = std::make_unique<Pipeline>(*device_, vertShaderCode, fragShaderCode, configInfo);
    }

    void TerrainRenderer::computeFrustumVisibleChunks(
        const glm::vec3& cameraPos, const glm::mat4& viewProj,
        float cs, ChunkLookupFn lookupFn, void* lookupContext)
    {
        reachedSet_.clear();

        // Underground detection.
        // Note: this tests "below highest terrain", not truly underground.
        // Indoor/cave scenarios may need refinement.
        int camGx = static_cast<int>(std::floor(cameraPos.x / cs));
        int camGz = static_cast<int>(std::floor(cameraPos.z / cs));
        const Chunk* camChunk = lookupFn(camGx, camGz, lookupContext);
        if (camChunk) {
            int lx = static_cast<int>(cameraPos.x - camChunk->getWorldOrigin().x);
            int lz = static_cast<int>(cameraPos.z - camChunk->getWorldOrigin().z);
            if (lx >= 0 && lx < static_cast<int>(cs) && lz >= 0 && lz < static_cast<int>(cs))
                if (cameraPos.y < static_cast<float>(camChunk->getHeightAt(lx, lz)) - 1.0f)
                    return;  // Underground — occlusion disabled
        }

        auto& settings = RendererSettings::get();
        float maxDist = settings.farPlane;

        // Cache inverse viewProj — only recompute when viewProj changes
        if (!invVPCacheValid_ || viewProj != cachedViewProj_) {
            cachedInvVP_ = glm::inverse(viewProj);
            cachedViewProj_ = viewProj;
            invVPCacheValid_ = true;
        }

        int gridW = settings.occlusionGridW;
        int gridH = settings.occlusionGridH;

        // Only grow capacity when needed
        size_t required = static_cast<size_t>(gridW) * static_cast<size_t>(gridH) * 2;
        if (reachedSet_.bucket_count() < required)
            reachedSet_.reserve(required);

        for (int j = 0; j < gridH; ++j) {
            float v = (static_cast<float>(j) + 0.5f) / static_cast<float>(gridH);
            float clipY = 2.0f * v - 1.0f;

            for (int i = 0; i < gridW; ++i) {
                float u = (static_cast<float>(i) + 0.5f) / static_cast<float>(gridW);
                float clipX = 2.0f * u - 1.0f;

                // Unproject far-plane point to world space
                glm::vec4 farClip(clipX, clipY, 1.0f, 1.0f);
                glm::vec4 farWorld = cachedInvVP_ * farClip;
                if (farWorld.w == 0.0f) continue;
                glm::vec3 target = glm::vec3(farWorld) / farWorld.w;

                glm::vec3 dir = target - cameraPos;
                float dist = glm::length(dir);
                if (dist < 0.001f) continue;
                dir /= dist;

                // Clamp to far plane distance
                float rayDist = std::min(dist, maxDist);

                traceRay(cameraPos, dir, rayDist, cs, OCCLUSION_EPSILON,
                         lookupFn, lookupContext, reachedSet_);
            }
        }
    }

    void TerrainRenderer::render(VkCommandBuffer cmd, const std::vector<Chunk*>& chunks,
                                 const glm::mat4& viewProj, const glm::vec3& cameraPos,
                                 bool enableFrustumCulling, float worldHeight,
                                 ChunkLookupFn lookupFn, void* lookupContext,
                                  double* outFrustumMs, double* outDrawMs,
                                  uint32_t* outVisibleChunks,
                                  uint32_t* outVisibleSubChunks,
                                  uint32_t* outOcclusionTested,
                                  uint32_t* outOcclusionRemoved)
    {
        if (!pipeline_) return;

        bool currentDisableTextures = RendererSettings::get().disableTextures;
        if (currentDisableTextures != lastDisableTextures_) {
            lastDisableTextures_ = currentDisableTextures;
            pipeline_.reset();
            createPipeline(currentDisableTextures);
        }

        pipeline_->bind(cmd);

        if (textureCache_) {
            VkDescriptorSet texSet = textureCache_->getDescriptorSet();
            if (texSet != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipelineLayout_, 0, 1, &texSet, 0, nullptr);
            }
        }

        std::vector<Chunk*> visible;
        visible.reserve(chunks.size());

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
                if (chunk->getIndexCount() == 0) continue;
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
                if (chunk->getIndexCount() == 0) continue;
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

            computeFrustumVisibleChunks(cameraPos, viewProj, cs, lookupFn, lookupContext);

            std::vector<Chunk*> filtered;
            filtered.reserve(visible.size());
            for (Chunk* c : visible) {
                glm::vec3 d = (c->getWorldOrigin() + glm::vec3(cs * 0.5f, 0.0f, cs * 0.5f)) - cameraPos;
                if (d.x * d.x + d.y * d.y + d.z * d.z < nearDist * nearDist) {
                    filtered.push_back(c);
                    continue;
                }
                int cx = static_cast<int>(std::floor(c->getWorldOrigin().x / cs));
                int cz = static_cast<int>(std::floor(c->getWorldOrigin().z / cs));
                if (reachedSet_.count(makeChunkKey(cx, cz))) {
                    filtered.push_back(c);
                } else {
                    occlusionRemoved++;
                }
                occlusionTested++;
            }
            visible.swap(filtered);
        }
        if (outOcclusionTested) *outOcclusionTested = occlusionTested;
        if (outOcclusionRemoved) *outOcclusionRemoved = occlusionRemoved;

        if (outFrustumMs)
            *outFrustumMs = (TimeUtil::uptimeSeconds() - frustumStart) * 1000.0;

        double drawStart = TimeUtil::uptimeSeconds();
        TerrainPushConstants pc{};
        pc.viewProj = viewProj;
        for (Chunk* chunk : visible) {
            pc.chunkOrigin = glm::vec4(chunk->getWorldOrigin(), 0.0f);
            vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TerrainPushConstants), &pc);
            chunk->bindAndDraw(cmd);
        }
        if (outDrawMs)
            *outDrawMs = (TimeUtil::uptimeSeconds() - drawStart) * 1000.0;

        if (outVisibleChunks)
            *outVisibleChunks = static_cast<uint32_t>(visible.size());
        if (outVisibleSubChunks)
            *outVisibleSubChunks = visibleSubCount;
    }

    void TerrainRenderer::onRenderPassChanged(VkRenderPass renderPass) {
        renderPass_ = renderPass;
    }

} // namespace lve
