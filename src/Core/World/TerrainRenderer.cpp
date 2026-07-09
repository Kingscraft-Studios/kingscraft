#include "Core/World/TerrainRenderer.hpp"
#include "Core/Frustum.hpp"
#include "Vulkan/TextureCache.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Util/TimeUtil.hpp"
#include "Util/Preloader.hpp"
#include "Core/World/Chunk.hpp"
#include <algorithm>
#include <stdexcept>

namespace lve {

    struct TerrainPushConstants {
        glm::mat4 viewProj;     // 64 bytes
        glm::vec4 chunkOrigin;  // 16 bytes
    };

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
                                  double* outFrustumMs, double* outDrawMs,
                                  uint32_t* outVisibleChunks)
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
            }
        }

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
    }

    void TerrainRenderer::onRenderPassChanged(VkRenderPass renderPass) {
        renderPass_ = renderPass;
    }

} // namespace lve
