// TODO: SSBO Style System
// Two-level indirection: elementId → styleIndex → GpuStyle
// Binding 0: UBO (projection matrix, vertex shader)
// Binding 1: font atlas sampler (fragment shader)
// Binding 2: stylePool SSBO — GpuStyle[64] (vertex shader)
// Binding 3: elementStyles SSBO — uint32_t[1024] (vertex shader)
// Vertex shader does both lookups, passes interpolated style to FS.
// Fragment shader has ZERO SSBO reads — uses varyings only.

#include "UI/Engine/UiRenderer.hpp"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <bits/this_thread_sleep.h>
#include <glm/gtc/matrix_transform.hpp>

#include "Bus/MessageBus.hpp"
#include "Threads/Engine.hpp"
#include "Threads/RenderThread.hpp"

namespace kc {

    UiRenderer::UiRenderer(Device& device, DescriptorManager& descriptorManager, VkExtent2D extent)
        : device_(device), descriptorManager_(descriptorManager), extent_(extent) {}

    UiRenderer::~UiRenderer() {
        shutdown();
    }

    void UiRenderer::init() {
        createDescriptorSetLayouts();
        createDescriptorPools();
        createUniformBuffer();
        fallbackTexture_ = std::make_unique<FallbackTexture>(device_);
        blockTexView_ = fallbackTexture_->getView();
        blockTexSampler_ = fallbackTexture_->getSampler();
        allocateDescriptorSets();
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            updateUiDescriptorSet(i);
        }
        updateUniformBuffer();
        initialized_ = true;
    }

    void UiRenderer::shutdown() {
        if (!initialized_) return;
        for (auto& slot : retiredPipelines_) {
            for (auto& rp : slot) {
                rp.pipeline.reset();
                rp.layout.reset();
            }
            slot.clear();
        }
        uiPipeline_.reset();
        pipelineLayout_.reset();
        blockTexLayout_.reset();
        blockTexPool_.reset();
        uiDescriptorSetLayout_.reset();
        uiDescriptorPool_.reset();
        stylePoolBuffer_.reset();
        elementStylesBuffer_.reset();
        uniformBuffer_.reset();
        fallbackTexture_.reset();
        blockTexView_ = VK_NULL_HANDLE;
        blockTexSampler_ = VK_NULL_HANDLE;
        blockTexDirty_ = false;
        targetRenderPass_ = VK_NULL_HANDLE;
        initialized_ = false;
    }

    void UiRenderer::resize(VkExtent2D extent) {
        if (!initialized_) return;
        extent_ = extent;
        updateUniformBuffer();
    }

    void UiRenderer::render(VkCommandBuffer cmd, UiBatchQueue& batchQueue, uint32_t frameIndex) {
        if (!initialized_ || extent_.width == 0 || extent_.height == 0) return;
        if (atlasView_ == VK_NULL_HANDLE) return;
        if (targetRenderPass_ == VK_NULL_HANDLE) return;

        currentFrameIndex_ = frameIndex;
        for (auto& rp : retiredPipelines_[frameIndex]) {
            rp.pipeline.reset();
            rp.layout.reset();
        }
        retiredPipelines_[frameIndex].clear();

        if (!uiPipeline_) {
            createPipeline();
        }
        if (!uiPipeline_) return;

        uiPipeline_->bind(cmd);

        VkViewport viewport{0.0f, 0.0f,
            static_cast<float>(extent_.width), static_cast<float>(extent_.height),
            0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{{0, 0}, extent_};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        if (uiDescDirtyMask_ & (1u << frameIndex)) {
            updateUiDescriptorSet(frameIndex);
            uiDescDirtyMask_ &= ~(1u << frameIndex);
        }

        VkDescriptorSet bindSets[2] = { uiDescriptorSets_[frameIndex], blockTexSets_[frameIndex] };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout_->getHandle(), 0, 2, bindSets, 0, nullptr);

        batchQueue.flush(cmd);
    }

    void UiRenderer::setTargetRenderPass(VkRenderPass renderPass) {
        if (renderPass == targetRenderPass_) return;
        targetRenderPass_ = renderPass;

        if (!uiPipeline_) return;

        // The pipeline is only compatible with the pass it was built for, so
        // retire the old one and rebuild against the new pass on next use.
        uint32_t retireSlot = (currentFrameIndex_ + 1) % MAX_FRAMES_IN_FLIGHT;
        RetiredPipeline rp;
        rp.pipeline = std::move(uiPipeline_);
        rp.layout = std::move(pipelineLayout_);
        retiredPipelines_[retireSlot].push_back(std::move(rp));
    }

    void UiRenderer::setFontAtlas(VkImageView imageView, VkSampler sampler) {
        atlasView_ = imageView;
        atlasSampler_ = sampler;
        atlasDirty_ = true;
        uiDescDirtyMask_ |= (1u << MAX_FRAMES_IN_FLIGHT) - 1;
    }

    void UiRenderer::setBlockTexture(VkImageView imageView, VkSampler sampler) {
        // A null view/sampler must never reach a descriptor write, and the UI may
        // ask for the atlas before the cache has built it. Keep whatever is
        // currently bound (the fallback checkerboard, at worst) instead.
        if (imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) return;

        blockTexView_ = imageView;
        blockTexSampler_ = sampler;
        blockTexDirty_ = true;
        uiDescDirtyMask_ |= (1u << MAX_FRAMES_IN_FLIGHT) - 1;
    }

    void UiRenderer::uploadStylePool(const void* data, uint32_t count) {
        if (!stylePoolBuffer_) return;
        uint32_t bytes = std::min(count, MAX_STYLES) * sizeof(GpuStyle);
        stylePoolBuffer_->write(data, 0, bytes);
    }

    void UiRenderer::uploadElementStyles(const void* data, uint32_t firstElement, uint32_t count) {
        if (!elementStylesBuffer_) return;
        uint32_t bytes = std::min(count, MAX_ELEMENTS - firstElement) * sizeof(uint32_t);
        elementStylesBuffer_->write(data, firstElement * sizeof(uint32_t), bytes);
    }

    void UiRenderer::createDescriptorSetLayouts() {
        std::vector<VkDescriptorSetLayoutBinding> uiBindings(4);
        uiBindings[0].binding = 0;
        uiBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uiBindings[0].descriptorCount = 1;
        uiBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        uiBindings[1].binding = 1;
        uiBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        uiBindings[1].descriptorCount = 1;
        uiBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        uiBindings[2].binding = 2;
        uiBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        uiBindings[2].descriptorCount = 1;
        uiBindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        uiBindings[3].binding = 3;
        uiBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        uiBindings[3].descriptorCount = 1;
        uiBindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        uiDescriptorSetLayout_ = DescriptorSetLayout::adopt(
            device_, descriptorManager_.createLayout(uiBindings));

        std::vector<VkDescriptorSetLayoutBinding> blockBindings(1);
        blockBindings[0].binding = 0;
        blockBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        blockBindings[0].descriptorCount = 1;
        blockBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        blockTexLayout_ = DescriptorSetLayout::adopt(
            device_, descriptorManager_.createLayout(blockBindings));
    }

    void UiRenderer::createDescriptorPools() {
        std::vector<VkDescriptorPoolSize> uiPoolSizes(4);
        uiPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uiPoolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
        uiPoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        uiPoolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;
        uiPoolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        uiPoolSizes[2].descriptorCount = MAX_FRAMES_IN_FLIGHT;
        uiPoolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        uiPoolSizes[3].descriptorCount = MAX_FRAMES_IN_FLIGHT;

        uiDescriptorPool_ = DescriptorPool::adopt(
            device_, descriptorManager_.createPool(uiPoolSizes, MAX_FRAMES_IN_FLIGHT));

        std::vector<VkDescriptorPoolSize> blockPoolSizes(1);
        blockPoolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        blockPoolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;

        blockTexPool_ = DescriptorPool::adopt(
            device_, descriptorManager_.createPool(blockPoolSizes, MAX_FRAMES_IN_FLIGHT));
    }

    void UiRenderer::allocateDescriptorSets() {
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            uiDescriptorSets_[i] = descriptorManager_.allocateSet(
                uiDescriptorPool_->getHandle(), uiDescriptorSetLayout_->getHandle());
            blockTexSets_[i] = descriptorManager_.allocateSet(
                blockTexPool_->getHandle(), blockTexLayout_->getHandle());
        }
    }

    void UiRenderer::createPipeline() {
        VkDescriptorSetLayout setLayouts[2] = {
            uiDescriptorSetLayout_->getHandle(),
            blockTexLayout_->getHandle(),
        };

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 2;
        layoutInfo.pSetLayouts = setLayouts;

        pipelineLayout_ = std::make_unique<PipelineLayout>(device_, layoutInfo);

        auto& shaderCache = Engine::Get().getCacheManager().getShaderCache();
        auto& vertCode = shaderCache.get("ui", ShaderStage::Vertex);
        auto& fragCode = shaderCache.get("ui", ShaderStage::Fragment);

        PipelineConfigInfo configInfo{};
        Pipeline::applyDefaultPipelineConfigInfo(configInfo);

        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = 0;
        bindingDesc.stride = sizeof(UiVertex);
        bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        // pos (loc 0), uv (loc 1), color (loc 2), elementId (loc 3)
        std::vector<VkVertexInputAttributeDescription> attributeDescs(4);
        attributeDescs[0].binding = 0;
        attributeDescs[0].location = 0;
        attributeDescs[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescs[0].offset = static_cast<uint32_t>(offsetof(UiVertex, pos));

        attributeDescs[1].binding = 0;
        attributeDescs[1].location = 1;
        attributeDescs[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescs[1].offset = static_cast<uint32_t>(offsetof(UiVertex, uv));

        attributeDescs[2].binding = 0;
        attributeDescs[2].location = 2;
        attributeDescs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescs[2].offset = static_cast<uint32_t>(offsetof(UiVertex, color));

        attributeDescs[3].binding = 0;
        attributeDescs[3].location = 3;
        attributeDescs[3].format = VK_FORMAT_R32_UINT;
        attributeDescs[3].offset = static_cast<uint32_t>(offsetof(UiVertex, elementId));

        configInfo.bindingDescriptions = {bindingDesc};
        configInfo.attributeDescriptions = {attributeDescs.begin(), attributeDescs.end()};

        configInfo.blendAttachmentState.blendEnable = VK_TRUE;
        configInfo.blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        configInfo.blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        configInfo.blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
        configInfo.blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        configInfo.blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        configInfo.blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

        configInfo.depthStencilInfo.depthTestEnable = VK_FALSE;
        configInfo.depthStencilInfo.depthWriteEnable = VK_FALSE;

        configInfo.renderPass = targetRenderPass_;
        configInfo.pipelineLayout = pipelineLayout_->getHandle();

        uiPipeline_ = std::make_unique<Pipeline>(device_, vertCode, fragCode, configInfo);
    }

    void UiRenderer::createUniformBuffer() {
        uniformBuffer_ = std::make_unique<Buffer>(
            device_, sizeof(glm::mat4),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    void UiRenderer::updateUniformBuffer() {
        // y grows downwards in UI space, while Vulkan NDC y = -1 is the top
        // framebuffer row, so top/bottom go in ascending order. The old
        // offscreen path had this inverted and undid it in composite.vert;
        // with the UI drawn directly into the world pass there is no flip to
        // compensate for.
        glm::mat4 proj = glm::ortho(
            0.0f, static_cast<float>(extent_.width),
            0.0f, static_cast<float>(extent_.height),
            -1.0f, 1.0f);
        uniformBuffer_->write(&proj, 0, sizeof(glm::mat4));
    }

    void UiRenderer::updateUiDescriptorSet(uint32_t frameIndex) {
        // Allocate SSBOs lazily on first descriptor update
        if (!stylePoolBuffer_) {
            stylePoolBuffer_ = std::make_unique<Buffer>(
                device_, MAX_STYLES * sizeof(GpuStyle),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            stylePoolInfo_.buffer = stylePoolBuffer_->getHandle();
            stylePoolInfo_.offset = 0;
            stylePoolInfo_.range = MAX_STYLES * sizeof(GpuStyle);
        }
        if (!elementStylesBuffer_) {
            elementStylesBuffer_ = std::make_unique<Buffer>(
                device_, MAX_ELEMENTS * sizeof(uint32_t),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            elementStylesInfo_.buffer = elementStylesBuffer_->getHandle();
            elementStylesInfo_.offset = 0;
            elementStylesInfo_.range = MAX_ELEMENTS * sizeof(uint32_t);
        }

        // Write UBO binding (0)
        VkDescriptorBufferInfo uboBufferInfo{};
        uboBufferInfo.buffer = uniformBuffer_->getHandle();
        uboBufferInfo.offset = 0;
        uboBufferInfo.range = sizeof(glm::mat4);

        VkWriteDescriptorSet uboWrite{};
        uboWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboWrite.dstSet = uiDescriptorSets_[frameIndex];
        uboWrite.dstBinding = 0;
        uboWrite.descriptorCount = 1;
        uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboWrite.pBufferInfo = &uboBufferInfo;

        // Write font atlas binding (1) — only if atlas is ready
        VkDescriptorImageInfo fontImageInfo{};
        fontImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        fontImageInfo.imageView = atlasView_;
        fontImageInfo.sampler = atlasSampler_;

        VkWriteDescriptorSet fontWrite{};
        fontWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        fontWrite.dstSet = uiDescriptorSets_[frameIndex];
        fontWrite.dstBinding = 1;
        fontWrite.descriptorCount = 1;
        fontWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        fontWrite.pImageInfo = &fontImageInfo;

        // Write stylePool SSBO binding (2)
        VkWriteDescriptorSet poolWrite{};
        poolWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        poolWrite.dstSet = uiDescriptorSets_[frameIndex];
        poolWrite.dstBinding = 2;
        poolWrite.descriptorCount = 1;
        poolWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolWrite.pBufferInfo = &stylePoolInfo_;

        // Write elementStyles SSBO binding (3)
        VkWriteDescriptorSet elemWrite{};
        elemWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        elemWrite.dstSet = uiDescriptorSets_[frameIndex];
        elemWrite.dstBinding = 3;
        elemWrite.descriptorCount = 1;
        elemWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        elemWrite.pBufferInfo = &elementStylesInfo_;

        // Write block texture binding (set 1, binding 0)
        VkDescriptorImageInfo blockImageInfo{};
        blockImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        blockImageInfo.imageView = blockTexView_;
        blockImageInfo.sampler = blockTexSampler_;

        VkWriteDescriptorSet blockWrite{};
        blockWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        blockWrite.dstSet = blockTexSets_[frameIndex];
        blockWrite.dstBinding = 0;
        blockWrite.descriptorCount = 1;
        blockWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        blockWrite.pImageInfo = &blockImageInfo;

        // Collect all writes
        VkWriteDescriptorSet writes[6];
        uint32_t writeCount = 0;
        writes[writeCount++] = uboWrite;
        writes[writeCount++] = poolWrite;
        writes[writeCount++] = elemWrite;
        if (atlasView_ != VK_NULL_HANDLE) {
            writes[writeCount++] = fontWrite;
        }
        writes[writeCount++] = blockWrite;

        descriptorManager_.updateDescriptorSets(
            std::vector<VkWriteDescriptorSet>(writes, writes + writeCount));
    }

} // namespace kc
