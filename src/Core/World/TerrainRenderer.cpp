#include "Core/World/TerrainRenderer.hpp"
#include "Core/Frustum.hpp"
#include "Vulkan/TextureCache.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Util/TimeUtil.hpp"
#include "Util/Preloader.hpp"
#include "Core/World/Chunk.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lve {

    struct TerrainPushConstants {
        glm::mat4 viewProj;     // 64 bytes
        glm::vec4 chunkOrigin;  // 16 bytes
    };

    static bool isChunkOccluded(const glm::vec3& cameraPos, const Chunk* chunk,
                                 float cs, ChunkLookupFn lookupFn, void* lookupContext) {
        if (chunk->getMaxHeight() == 0) return false;

        glm::vec3 origin = chunk->getWorldOrigin();

        // 5 test points: center + 4 top-face corners
        float pts[5][2] = {
            {origin.x + cs * 0.5f, origin.z + cs * 0.5f},
            {origin.x,             origin.z},
            {origin.x + cs,        origin.z},
            {origin.x,             origin.z + cs},
            {origin.x + cs,        origin.z + cs},
        };

        // Check if camera is underground
        int camGx = static_cast<int>(std::floor(cameraPos.x / cs));
        int camGz = static_cast<int>(std::floor(cameraPos.z / cs));
        const Chunk* camChunk = lookupFn(camGx, camGz, lookupContext);
        bool underground = false;
        if (camChunk) {
            int lx = static_cast<int>(cameraPos.x - camChunk->getWorldOrigin().x);
            int lz = static_cast<int>(cameraPos.z - camChunk->getWorldOrigin().z);
            if (lx >= 0 && lx < static_cast<int>(cs) && lz >= 0 && lz < static_cast<int>(cs))
                if (static_cast<int>(cameraPos.y) <= static_cast<int>(camChunk->getHeightAt(lx, lz)))
                    underground = true;
        }
        if (underground) return false;

        for (int pi = 0; pi < 5; ++pi) {
            float wx = pts[pi][0];
            float wz = pts[pi][1];

            int lx = static_cast<int>(wx - origin.x);
            int lz = static_cast<int>(wz - origin.z);
            if (lx < 0 || lx >= static_cast<int>(cs) || lz < 0 || lz >= static_cast<int>(cs))
                continue;
            float targetY = static_cast<float>(chunk->getHeightAt(lx, lz));
            if (targetY <= 0.0f) continue;

            glm::vec3 target(wx, targetY, wz);
            glm::vec3 dir = glm::normalize(target - cameraPos);
            float dist = glm::distance(target, cameraPos);

            // Walk through chunk grid toward the target
            for (float d = cs; d < dist; d += cs) {
                glm::vec3 p = cameraPos + dir * d;
                int gx = static_cast<int>(std::floor(p.x / cs));
                int gz = static_cast<int>(std::floor(p.z / cs));
                const Chunk* ic = lookupFn(gx, gz, lookupContext);
                if (!ic) continue;
                if (ic->getMaxHeight() == 0) continue;

                glm::vec3 io = ic->getWorldOrigin();
                int ilx = static_cast<int>(p.x - io.x);
                int ilz = static_cast<int>(p.z - io.z);
                if (ilx < 0 || ilx >= static_cast<int>(cs) || ilz < 0 || ilz >= static_cast<int>(cs))
                    continue;

                // +0.5f epsilon to prevent surface-grazing false positives
                if (static_cast<float>(ic->getHeightAt(ilx, ilz)) > p.y + 0.5f)
                    return true;
            }
        }
        return false;
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
                glm::vec3 min = origin;
                glm::vec3 max = origin + glm::vec3(chunkSize, worldHeight, chunkSize);
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

        // Occlusion culling (front-to-back sorted)
        uint32_t occlusionTested = 0;
        uint32_t occlusionRemoved = 0;
        if (lookupFn && enableFrustumCulling && RendererSettings::get().enableOcclusionCulling && !visible.empty()) {
            float cs = static_cast<float>(visible[0]->getVerticesPerAxis() - 1);
            float nearDist = cs * 2.0f;
            std::vector<Chunk*> filtered;
            filtered.reserve(visible.size());
            for (Chunk* c : visible) {
                glm::vec3 d = (c->getWorldOrigin() + glm::vec3(cs * 0.5f, 0.0f, cs * 0.5f)) - cameraPos;
                if (d.x * d.x + d.y * d.y + d.z * d.z < nearDist * nearDist) {
                    filtered.push_back(c);
                    continue;
                }
                occlusionTested++;
                if (!isChunkOccluded(cameraPos, c, cs, lookupFn, lookupContext))
                    filtered.push_back(c);
                else
                    occlusionRemoved++;
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
