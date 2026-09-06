#include "Renderer/WorldRenderer.hpp"

#include <array>
#include <iostream>

#include "Core/World/GpuChunkData.hpp"
#include "Renderer/FrameScene.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Threads/RenderThread.hpp"
#include "../../include/Core/Bootstrapper.hpp"
#include "Core/Runtime.hpp"

namespace kc {
    struct TerrainPushConstants {
        glm::mat4 viewProj;     // 64 bytes
        glm::vec4 chunkOrigin;  // 16 bytes
    };

    struct HighlightPushConstants {
        glm::mat4 viewProj;     // 64 bytes
    };

    static constexpr uint32_t HIGHLIGHT_VERTEX_COUNT = 24;

    WorldRenderer::~WorldRenderer() {
        cleanup();
    }

    void WorldRenderer::init(Device &device, TextureCache &textureCache, VkRenderPass renderPass) {
        device_ = &device;
        textureCache_ = &textureCache;
        renderPass_ = renderPass;
        createPipelineLayout();
        createPipeline(RendererSettings::get().disableTextures);
        createHighlightPipeline();
    }

    void WorldRenderer::cleanup() {
        highlightPipeline_.reset();
        highlightVertexBuffer_.reset();
        pipeline_.reset();
        pipelineLayout_.reset();
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

        pipelineLayout_ = std::make_unique<PipelineLayout>(*device_, layoutInfo);
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
        configInfo.pipelineLayout = pipelineLayout_->getHandle();

        pipeline_ = std::make_unique<Pipeline>(*device_, vertShaderCode, fragShaderCode, configInfo);
    }

    void WorldRenderer::createHighlightPipeline() {
        auto& vertShaderCode = Bootstrapper::Get().getShader("resources/shaders/highlight.vert.spv");
        auto& fragShaderCode = Bootstrapper::Get().getShader("resources/shaders/highlight.frag.spv");

        PipelineConfigInfo configInfo{};
        Pipeline::defaultPipelineConfigInfo(configInfo);
        configInfo.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        configInfo.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
        configInfo.depthStencilInfo.depthTestEnable = VK_TRUE;
        configInfo.depthStencilInfo.depthWriteEnable = VK_FALSE;
        configInfo.renderPass = renderPass_;
        configInfo.pipelineLayout = pipelineLayout_->getHandle();

        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = 0;
        bindingDesc.stride = sizeof(glm::vec3);
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attrDesc{};
        attrDesc.binding = 0;
        attrDesc.location = 0;
        attrDesc.format = VK_FORMAT_R32G32B32_SFLOAT;
        attrDesc.offset = 0;

        configInfo.bindingDescriptions = {bindingDesc};
        configInfo.attributeDescriptions = {attrDesc};

        highlightPipeline_ = std::make_unique<Pipeline>(*device_, vertShaderCode, fragShaderCode, configInfo);
        highlightVertexBuffer_ = std::make_unique<Buffer>(*device_,
            sizeof(glm::vec3) * HIGHLIGHT_VERTEX_COUNT,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
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
                                        pipelineLayout_->getHandle(), 0, 1, &texSet, 0, nullptr);
            }
        }

        TerrainPushConstants pc{};
        pc.viewProj = viewProj;
        for (const TerrainDraw& draw : scene.terrain.draws) {
            pc.chunkOrigin = glm::vec4(draw.worldOrigin, 0.0f);
            vkCmdPushConstants(cmd, pipelineLayout_->getHandle(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TerrainPushConstants), &pc);
            drawChunk(cmd, draw.chunkKey);
        }

        drawHighlight(cmd, viewProj, scene);
    }

    void WorldRenderer::drawChunk(VkCommandBuffer cmd, uint64_t chunkKey) {
        const GpuChunkData* data = Runtime::get().renderThread->getUploader().getChunkData(chunkKey);

        if (!data || !data->vertexBuffer || !data->indexBuffer) return;
        // The buffers are allocated before the async upload fence signals, so the
        // handles can exist while the GPU is still copying into them. Wait until
        // the upload fence is signaled before issuing drawIndexed to avoid drawing
        // un-arrived data. Once signaled the fence stays signaled for this chunk's
        // lifetime (created unsignaled, never reset), so this is safe to check each frame.
        if (data->uploadFence && data->uploadFence->status() != VK_SUCCESS)
            return;

        VkBuffer vb;
        VkBuffer ib;
        uint32_t count;

        vb = data->vertexBuffer->getHandle();
        ib = data->indexBuffer->getHandle();
        count = data->indexCount;

        VkBuffer bufs[] = {vb};
        VkDeviceSize offset[] = {0};

        vkCmdBindVertexBuffers(cmd, 0, 1, bufs, offset);
        vkCmdBindIndexBuffer(cmd, ib, 0, VK_INDEX_TYPE_UINT16);

        vkCmdDrawIndexed(cmd, count, 1, 0, 0, 0);
    }

    void WorldRenderer::drawHighlight(VkCommandBuffer cmd, const glm::mat4 &viewProj, const FrameScene& scene) {
        if (!highlightPipeline_ || !highlightVertexBuffer_ || !scene.highlight.enabled) return;

        const float x = scene.highlight.position.x;
        const float y = scene.highlight.position.y;
        const float z = scene.highlight.position.z;

        std::array<glm::vec3, 8> corners = {
            glm::vec3{x,     y,     z},
            glm::vec3{x + 1, y,     z},
            glm::vec3{x + 1, y + 1, z},
            glm::vec3{x,     y + 1, z},
            glm::vec3{x,     y,     z + 1},
            glm::vec3{x + 1, y,     z + 1},
            glm::vec3{x + 1, y + 1, z + 1},
            glm::vec3{x,     y + 1, z + 1},
        };

        const uint32_t edgePairs[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7},
        };

        std::array<glm::vec3, HIGHLIGHT_VERTEX_COUNT> verts{};
        for (uint32_t i = 0; i < 12; ++i) {
            verts[i * 2]     = corners[edgePairs[i][0]];
            verts[i * 2 + 1] = corners[edgePairs[i][1]];
        }

        highlightVertexBuffer_->write(verts.data(), 0, sizeof(verts));

        highlightPipeline_->bind(cmd);

        HighlightPushConstants pc{};
        pc.viewProj = viewProj;
        vkCmdPushConstants(cmd, pipelineLayout_->getHandle(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(HighlightPushConstants), &pc);

        VkBuffer vb = highlightVertexBuffer_->getHandle();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
        vkCmdDraw(cmd, HIGHLIGHT_VERTEX_COUNT, 1, 0, 0);
    }

    void WorldRenderer::onRenderPassChanged(VkRenderPass renderPass) {
        renderPass_ = renderPass;
        pipeline_.reset();
        createPipeline(RendererSettings::get().disableTextures);
        highlightPipeline_.reset();
        createHighlightPipeline();
    }
}
