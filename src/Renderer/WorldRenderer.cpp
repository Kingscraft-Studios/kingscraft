#include "Renderer/WorldRenderer.hpp"

#include <iostream>

#include "Core/World/GpuChunkData.hpp"
#include "Renderer/FrameScene.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Threads/Renderer.hpp"
#include "../../include/Core/Bootstrapper.hpp"

namespace lve {
    struct TerrainPushConstants {
        glm::mat4 viewProj;     // 64 bytes
        glm::vec4 chunkOrigin;  // 16 bytes
    };

    WorldRenderer::~WorldRenderer() {
        cleanup();
    }

    void WorldRenderer::init(Device &device, TextureCache &textureCache, VkRenderPass renderPass) {
        device_ = &device;
        textureCache_ = &textureCache;
        renderPass_ = renderPass;
        createPipelineLayout();
        createPipeline(RendererSettings::get().disableTextures);
    }

    void WorldRenderer::cleanup() {
        pipeline_.reset();
        if (pipelineLayout_ != VK_NULL_HANDLE && device_) {
            vkDestroyPipelineLayout(device_->device(), pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
    }

    void WorldRenderer::createPipelineLayout() {
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(TerrainPushConstants);

        VkDescriptorSetLayout texLayout = textureCache_->getLayout();

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

    void WorldRenderer::createPipeline(bool disableTextures) {
        auto& vertShaderCode = Bootstrapper::Get().getShader("resources/shaders/terrain.vert.spv");
        auto& fragShaderCode = Bootstrapper::Get().getShader("resources/shaders/terrain.frag.spv");

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

    void WorldRenderer::render(VkCommandBuffer cmd, const glm::mat4 &viewProj, const FrameScene& scene) {
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

        TerrainPushConstants pc{};
        pc.viewProj = viewProj;
        for (const TerrainDraw& draw : scene.terrain.draws) {
            pc.chunkOrigin = glm::vec4(draw.worldOrigin, 0.0f);
            vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TerrainPushConstants), &pc);
            drawChunk(cmd, draw.chunkKey);
        }
    }

    void WorldRenderer::drawChunk(VkCommandBuffer cmd, uint64_t chunkKey) {
        const GpuChunkData* data = RenderThread::getInstance().getUploader().getChunkData(chunkKey);

        if (!data || !data->vertexBuffer || !data->indexBuffer) return;

        VkBuffer vb;
        VkBuffer ib;
        uint32_t count;

        // TODO: Verify that The Chunks are Uploaded Correctly
        vb = data->vertexBuffer->getHandle();
        ib = data->indexBuffer->getHandle();
        count = data->indexCount;

        VkBuffer bufs[] = {vb};
        VkDeviceSize offset[] = {0};

        vkCmdBindVertexBuffers(cmd, 0, 1, bufs, offset);
        vkCmdBindIndexBuffer(cmd, ib, 0, VK_INDEX_TYPE_UINT16);

        vkCmdDrawIndexed(cmd, count, 1, 0, 0, 0);
    }

    void WorldRenderer::onRenderPassChanged(VkRenderPass renderPass) {
        renderPass_ = renderPass;
    }
}
