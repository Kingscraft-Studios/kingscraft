// TODO: SSBO Style System
// Two-level indirection: elementId → styleIndex → GpuStyle
// Binding 0: UBO (projection matrix, vertex shader)
// Binding 1: font atlas sampler (fragment shader)
// Binding 2: stylePool SSBO — GpuStyle[64] (vertex shader)
// Binding 3: elementStyles SSBO — uint32_t[1024] (vertex shader)
// Vertex shader does both lookups, passes interpolated style to FS.
// Fragment shader has ZERO SSBO reads — uses varyings only.

#include "UI/Engine/UiRenderer.hpp"
#include "../../../include/Core/Bootstrapper.hpp"

#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <bits/this_thread_sleep.h>
#include <glm/gtc/matrix_transform.hpp>

#include "Bus/MessageBus.hpp"
#include "Threads/RenderThread.hpp"

namespace kc {

    UiRenderer::UiRenderer(Device& device, DescriptorManager& descriptorManager, VkExtent2D extent)
        : device_(device), descriptorManager_(descriptorManager), extent_(extent) {}

    UiRenderer::~UiRenderer() {
        shutdown();
    }

    void UiRenderer::init() {
        offscreenRenderPass_ = RenderPass::createOffscreen(device_, VK_FORMAT_R8G8B8A8_UNORM);
        createDescriptorSetLayouts();
        createDescriptorPools();
        createUniformBuffer();
        createDummyTexture();
        offscreenTarget_ = std::make_unique<OffscreenTarget>(
            device_, extent_, VK_FORMAT_R8G8B8A8_UNORM, offscreenRenderPass_->getHandle());
        allocateDescriptorSets();
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            updateUiDescriptorSet(i);
            updateCompositeDescriptorSet(i);
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
        compositePipeline_.reset();
        pipelineLayout_.reset();
        compositePipelineLayout_.reset();
        offscreenTarget_.reset();
        blockTexLayout_.reset();
        blockTexPool_.reset();
        uiDescriptorSetLayout_.reset();
        compositeDescriptorSetLayout_.reset();
        uiDescriptorPool_.reset();
        compositeDescriptorPool_.reset();
        offscreenRenderPass_.reset();
        stylePoolBuffer_.reset();
        elementStylesBuffer_.reset();
        uniformBuffer_.reset();
        destroyDummyTexture();
        initialized_ = false;
    }

    void UiRenderer::resize(VkExtent2D extent) {
        if (!initialized_) return;
        extent_ = extent;
        offscreenTarget_->resize(extent_, offscreenRenderPass_->getHandle());
        compositeDescDirtyMask_ |= (1u << MAX_FRAMES_IN_FLIGHT) - 1;
        updateUniformBuffer();
    }

    void UiRenderer::renderOffscreen(VkCommandBuffer cmd, UiBatchQueue& batchQueue, uint32_t frameIndex) {
        if (!initialized_ || extent_.width == 0 || extent_.height == 0) return;
        if (atlasView_ == VK_NULL_HANDLE) return;

        currentFrameIndex_ = frameIndex;
        for (auto& rp : retiredPipelines_[frameIndex]) {
            rp.pipeline.reset();
            rp.layout.reset();
        }
        retiredPipelines_[frameIndex].clear();

        if (!uiPipeline_) {
            createPipeline();
        }

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.image = offscreenTarget_->getImage();
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkClearValue clear = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
        VkRenderPassBeginInfo passInfo{};
        passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        passInfo.renderPass = offscreenRenderPass_->getHandle();
        passInfo.framebuffer = offscreenTarget_->getFramebuffer();
        passInfo.renderArea = {{0, 0}, extent_};
        passInfo.clearValueCount = 1;
        passInfo.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

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

        vkCmdEndRenderPass(cmd);
    }

    void UiRenderer::composite(VkCommandBuffer cmd, uint32_t frameIndex) {
        if (!initialized_ || extent_.width == 0 || extent_.height == 0) return;

        if (compositeDescDirtyMask_ & (1u << frameIndex)) {
            updateCompositeDescriptorSet(frameIndex);
            compositeDescDirtyMask_ &= ~(1u << frameIndex);
        }

        VkViewport viewport{0.0f, 0.0f,
            static_cast<float>(extent_.width), static_cast<float>(extent_.height),
            0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{{0, 0}, extent_};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        compositePipeline_->bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                compositePipelineLayout_->getHandle(), 0, 1, &compositeDescriptorSets_[frameIndex], 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    void UiRenderer::setTargetRenderPass(VkRenderPass renderPass) {
        uint32_t retireSlot = (currentFrameIndex_ + 1) % MAX_FRAMES_IN_FLIGHT;

        RetiredPipeline rp;
        rp.pipeline = std::move(compositePipeline_);
        rp.layout = std::move(compositePipelineLayout_);
        retiredPipelines_[retireSlot].push_back(std::move(rp));

        createCompositePipeline(renderPass);
    }

    void UiRenderer::setFontAtlas(VkImageView imageView, VkSampler sampler) {
        atlasView_ = imageView;
        atlasSampler_ = sampler;
        atlasDirty_ = true;
        uiDescDirtyMask_ |= (1u << MAX_FRAMES_IN_FLIGHT) - 1;
    }

    void UiRenderer::setBlockTexture(VkImageView imageView, VkSampler sampler) {
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

        std::vector<VkDescriptorSetLayoutBinding> compBindings(1);
        compBindings[0].binding = 0;
        compBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        compBindings[0].descriptorCount = 1;
        compBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        compositeDescriptorSetLayout_ = DescriptorSetLayout::adopt(
            device_, descriptorManager_.createLayout(compBindings));
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

        std::vector<VkDescriptorPoolSize> compPoolSizes(1);
        compPoolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        compPoolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;

        compositeDescriptorPool_ = DescriptorPool::adopt(
            device_, descriptorManager_.createPool(compPoolSizes, MAX_FRAMES_IN_FLIGHT));
    }

    void UiRenderer::allocateDescriptorSets() {
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            uiDescriptorSets_[i] = descriptorManager_.allocateSet(
                uiDescriptorPool_->getHandle(), uiDescriptorSetLayout_->getHandle());
            blockTexSets_[i] = descriptorManager_.allocateSet(
                blockTexPool_->getHandle(), blockTexLayout_->getHandle());
            compositeDescriptorSets_[i] = descriptorManager_.allocateSet(
                compositeDescriptorPool_->getHandle(), compositeDescriptorSetLayout_->getHandle());
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

        auto& vertCode = Bootstrapper::Get().getShader("resources/shaders/ui.vert.spv");
        auto& fragCode = Bootstrapper::Get().getShader("resources/shaders/ui.frag.spv");

        PipelineConfigInfo configInfo{};
        Pipeline::defaultPipelineConfigInfo(configInfo);

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

        configInfo.renderPass = offscreenRenderPass_->getHandle();
        configInfo.pipelineLayout = pipelineLayout_->getHandle();

        uiPipeline_ = std::make_unique<Pipeline>(device_, vertCode, fragCode, configInfo);
    }

    void UiRenderer::createCompositePipeline(VkRenderPass renderPass) {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        VkDescriptorSetLayout compLayout = compositeDescriptorSetLayout_->getHandle();
        layoutInfo.pSetLayouts = &compLayout;

        compositePipelineLayout_ = std::make_unique<PipelineLayout>(device_, layoutInfo);

        auto& vertCode = Bootstrapper::Get().getShader("resources/shaders/composite.vert.spv");
        auto& fragCode = Bootstrapper::Get().getShader("resources/shaders/composite.frag.spv");

        PipelineConfigInfo configInfo{};
        Pipeline::defaultPipelineConfigInfo(configInfo);

        configInfo.bindingDescriptions = {};
        configInfo.attributeDescriptions = {};

        configInfo.blendAttachmentState.blendEnable = VK_TRUE;
        configInfo.blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        configInfo.blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        configInfo.blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
        configInfo.blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        configInfo.blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        configInfo.blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

        configInfo.depthStencilInfo.depthTestEnable = VK_FALSE;
        configInfo.depthStencilInfo.depthWriteEnable = VK_FALSE;

        configInfo.renderPass = renderPass;
        configInfo.pipelineLayout = compositePipelineLayout_->getHandle();

        compositePipeline_ = std::make_unique<Pipeline>(device_, vertCode, fragCode, configInfo);
    }

    void UiRenderer::createDummyTexture() {
        // 1x1 white pixel texture as fallback for block texture descriptor
        uint32_t white = 0xFFFFFFFF;
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        VkDeviceSize size = 4;
        device_.createBuffer(size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer, stagingMemory);
        void* mapped;
        vkMapMemory(device_.device(), stagingMemory, 0, size, 0, &mapped);
        std::memcpy(mapped, &white, size);
        vkUnmapMemory(device_.device(), stagingMemory);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = 1;
        imageInfo.extent.height = 1;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        dummyImage_ = std::make_unique<Image>(device_, imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        device_.transitionImageLayout(dummyImage_->getHandle(), VK_FORMAT_R8G8B8A8_SRGB,
                                      VK_IMAGE_LAYOUT_UNDEFINED,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      1, 1);
        device_.copyBufferToImage(stagingBuffer, dummyImage_->getHandle(), 1, 1, 1);
        device_.transitionImageLayout(dummyImage_->getHandle(), VK_FORMAT_R8G8B8A8_SRGB,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                      1, 1);

        vkDestroyBuffer(device_.device(), stagingBuffer, nullptr);
        vkFreeMemory(device_.device(), stagingMemory, nullptr);

        dummyImageView_ = std::make_unique<ImageView>(
            device_, dummyImage_->getHandle(), VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_ASPECT_COLOR_BIT, 1, 0,
            VK_IMAGE_VIEW_TYPE_2D_ARRAY, 1);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 1.0f;

        dummySampler_ = std::make_unique<Sampler>(device_, samplerInfo);

        // Use dummy as the initial block texture
        blockTexView_ = dummyImageView_->getHandle();
        blockTexSampler_ = dummySampler_->getHandle();
    }

    void UiRenderer::destroyDummyTexture() {
        dummySampler_.reset();
        dummyImageView_.reset();
        dummyImage_.reset();
    }

    void UiRenderer::createUniformBuffer() {
        uniformBuffer_ = std::make_unique<Buffer>(
            device_, sizeof(glm::mat4),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }

    void UiRenderer::updateUniformBuffer() {
        glm::mat4 proj = glm::ortho(
            0.0f, static_cast<float>(extent_.width),
            static_cast<float>(extent_.height), 0.0f,
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

    void UiRenderer::updateCompositeDescriptorSet(uint32_t frameIndex) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = offscreenTarget_->getImageView();
        imageInfo.sampler = offscreenTarget_->getSampler();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = compositeDescriptorSets_[frameIndex];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;

        descriptorManager_.updateDescriptorSets(std::vector<VkWriteDescriptorSet>{write});
    }

} // namespace kc
