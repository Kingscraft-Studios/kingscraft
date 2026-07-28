#include "Renderer/Bloom.hpp"
#include "../../include/Core/Bootstrapper.hpp"
#include <array>
#include <cstring>
#include <stdexcept>

namespace lve {


Bloom::Bloom(Device& device, VkExtent2D windowExtent, VkRenderPass sceneRenderPass,
             DescriptorManager& descriptorManager)
    : device_(device), descriptorManager_(descriptorManager),
      activeCompositeRenderPass_(sceneRenderPass), windowExtent_(windowExtent) {
    createOffscreen();
    createUniformBuffers();
    createDescriptors();
    createPipelines();
}

Bloom::~Bloom() {
    vkDeviceWaitIdle(device_.device());

    pipelines_.blurVert.reset();
    pipelines_.blurHorz.reset();
    pipelines_.glowPass.reset();

    for (auto& layout : {pipelineLayouts_.blur, pipelineLayouts_.scene}) {
        if (layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device_.device(), layout, nullptr);
    }
    for (auto& layout : {descriptorSetLayouts_.blur, descriptorSetLayouts_.scene}) {
        if (layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device_.device(), layout, nullptr);
    }
    if (descriptorPool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device_.device(), descriptorPool_, nullptr);

    for (auto& frame : frames_) {
        frame.blurUBO.reset();
        frame.glowUBO.reset();
    }

    if (offscreenPass_.sampler != VK_NULL_HANDLE)
        vkDestroySampler(device_.device(), offscreenPass_.sampler, nullptr);
    destroyOffscreenFramebuffers();
    offscreenRenderPass_.reset();
}

void Bloom::computeOffscreenDim(VkExtent2D windowExtent, int32_t& outW, int32_t& outH) {
    float aspect = static_cast<float>(windowExtent.width) / static_cast<float>(windowExtent.height);
    if (aspect >= 1.0f) {
        outW = static_cast<int32_t>(static_cast<float>(MAX_FB_DIM) * aspect + 0.5f);
        outH = MAX_FB_DIM;
    } else {
        outW = MAX_FB_DIM;
        outH = static_cast<int32_t>(static_cast<float>(MAX_FB_DIM) / aspect + 0.5f);
    }
}

void Bloom::destroyFramebuffer(FrameBuffer& fb) {
    if (fb.framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_.device(), fb.framebuffer, nullptr);
        fb.framebuffer = VK_NULL_HANDLE;
    }
    if (fb.color.view) { vkDestroyImageView(device_.device(), fb.color.view, nullptr); fb.color.view = VK_NULL_HANDLE; }
    if (fb.color.image) { vkDestroyImage(device_.device(), fb.color.image, nullptr); fb.color.image = VK_NULL_HANDLE; }
    if (fb.color.mem) { vkFreeMemory(device_.device(), fb.color.mem, nullptr); fb.color.mem = VK_NULL_HANDLE; }
    if (fb.depth.view) { vkDestroyImageView(device_.device(), fb.depth.view, nullptr); fb.depth.view = VK_NULL_HANDLE; }
    if (fb.depth.image) { vkDestroyImage(device_.device(), fb.depth.image, nullptr); fb.depth.image = VK_NULL_HANDLE; }
    if (fb.depth.mem) { vkFreeMemory(device_.device(), fb.depth.mem, nullptr); fb.depth.mem = VK_NULL_HANDLE; }
}

void Bloom::destroyOffscreenFramebuffers() {
    for (auto& fb : offscreenPass_.framebuffers)
        destroyFramebuffer(fb);
}

void Bloom::recreate(VkExtent2D windowExtent, VkRenderPass sceneRenderPass) {
    bool dimsChanged = windowExtent.width != windowExtent_.width || windowExtent.height != windowExtent_.height;
    windowExtent_ = windowExtent;

    if (dimsChanged) {
        destroyOffscreenFramebuffers();
        computeOffscreenDim(windowExtent_, offscreenPass_.width, offscreenPass_.height);
        createOffscreenFramebuffer(&offscreenPass_.framebuffers[0], FB_COLOR_FORMAT, depthFormat_);
        createOffscreenFramebuffer(&offscreenPass_.framebuffers[1], FB_COLOR_FORMAT, depthFormat_);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
            updateFrameDescriptor(i);
    }

    if (sceneRenderPass != activeCompositeRenderPass_) {
        activeCompositeRenderPass_ = VK_NULL_HANDLE;
    }
}

void Bloom::createOffscreenFramebuffer(FrameBuffer* fb, VkFormat colorFormat, VkFormat depthFormat) {
    int32_t w = offscreenPass_.width;
    int32_t h = offscreenPass_.height;

    VkImageCreateInfo image{};
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = colorFormat;
    image.extent.width = static_cast<uint32_t>(w);
    image.extent.height = static_cast<uint32_t>(h);
    image.extent.depth = 1;
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    device_.createImageWithInfo(image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, fb->color.image, fb->color.mem);

    VkImageViewCreateInfo view{};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = colorFormat;
    view.image = fb->color.image;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.baseMipLevel = 0;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.baseArrayLayer = 0;
    view.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_.device(), &view, nullptr, &fb->color.view) != VK_SUCCESS) {
        throw std::runtime_error("failed to create offscreen color image view!");
    }

    image.format = depthFormat;
    image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    device_.createImageWithInfo(image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, fb->depth.image, fb->depth.mem);

    view.format = depthFormat;
    view.image = fb->depth.image;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    if (vkCreateImageView(device_.device(), &view, nullptr, &fb->depth.view) != VK_SUCCESS) {
        throw std::runtime_error("failed to create offscreen depth image view!");
    }

    std::array<VkImageView, 2> attachments = {fb->color.view, fb->depth.view};

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = offscreenPass_.renderPass;
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = static_cast<uint32_t>(w);
    fbInfo.height = static_cast<uint32_t>(h);
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(device_.device(), &fbInfo, nullptr, &fb->framebuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create offscreen framebuffer!");
    }

    fb->descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    fb->descriptor.imageView = fb->color.view;
    fb->descriptor.sampler = offscreenPass_.sampler;
}

void Bloom::createOffscreen() {
    computeOffscreenDim(windowExtent_, offscreenPass_.width, offscreenPass_.height);

    VkFormat fbDepthFormat = device_.findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
    depthFormat_ = fbDepthFormat;

    std::array<VkAttachmentDescription, 2> attachments = {};

    attachments[0].format = FB_COLOR_FORMAT;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    attachments[1].format = fbDepthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    std::array<VkSubpassDependency, 3> dependencies{};

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    dependencies[0].dependencyFlags = 0;

    dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].dstSubpass = 0;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[2].srcSubpass = 0;
    dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    std::vector<RenderPass::AttachmentDescription> rpAttachments(2);
    rpAttachments[0].format = attachments[0].format;
    rpAttachments[0].loadOp = attachments[0].loadOp;
    rpAttachments[0].storeOp = attachments[0].storeOp;
    rpAttachments[0].initialLayout = attachments[0].initialLayout;
    rpAttachments[0].finalLayout = attachments[0].finalLayout;
    rpAttachments[1].format = attachments[1].format;
    rpAttachments[1].loadOp = attachments[1].loadOp;
    rpAttachments[1].storeOp = attachments[1].storeOp;
    rpAttachments[1].initialLayout = attachments[1].initialLayout;
    rpAttachments[1].finalLayout = attachments[1].finalLayout;

    offscreenRenderPass_ = std::make_unique<RenderPass>(
        device_, rpAttachments,
        std::vector<VkSubpassDescription>{subpass},
        std::vector<VkSubpassDependency>{dependencies.begin(), dependencies.end()});
    offscreenPass_.renderPass = offscreenRenderPass_->getHandle();

    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.mipLodBias = 0.0f;
    sampler.maxAnisotropy = 1.0f;
    sampler.minLod = 0.0f;
    sampler.maxLod = 1.0f;
    sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    if (vkCreateSampler(device_.device(), &sampler, nullptr, &offscreenPass_.sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create offscreen sampler!");
    }

    createOffscreenFramebuffer(&offscreenPass_.framebuffers[0], FB_COLOR_FORMAT, fbDepthFormat);
    createOffscreenFramebuffer(&offscreenPass_.framebuffers[1], FB_COLOR_FORMAT, fbDepthFormat);
}

void Bloom::createUniformBuffers() {
    for (auto& frame : frames_) {
        frame.glowUBO = std::make_unique<Buffer>(
            device_, sizeof(GlowUniformData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        frame.blurUBO = std::make_unique<Buffer>(
            device_, sizeof(BlurUniformData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
}

void Bloom::createDescriptors() {
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT * 4},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT * 4}
    };

    descriptorPool_ = descriptorManager_.createPool(poolSizes, MAX_FRAMES_IN_FLIGHT * 4);

    std::vector<VkDescriptorSetLayoutBinding> bindings;

    bindings = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    };
    descriptorSetLayouts_.blur = descriptorManager_.createLayout(bindings);

    bindings = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    };
    descriptorSetLayouts_.scene = descriptorManager_.createLayout(bindings);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        frames_[i].blurVert = descriptorManager_.allocateSet(descriptorPool_, descriptorSetLayouts_.blur);
        frames_[i].blurHorz = descriptorManager_.allocateSet(descriptorPool_, descriptorSetLayouts_.blur);
        frames_[i].scene = descriptorManager_.allocateSet(descriptorPool_, descriptorSetLayouts_.scene);

        VkDescriptorBufferInfo blurBufferInfo{};
        blurBufferInfo.buffer = frames_[i].blurUBO->getHandle();
        blurBufferInfo.offset = 0;
        blurBufferInfo.range = sizeof(BlurUniformData);

        VkDescriptorBufferInfo glowBufferInfo{};
        glowBufferInfo.buffer = frames_[i].glowUBO->getHandle();
        glowBufferInfo.offset = 0;
        glowBufferInfo.range = sizeof(GlowUniformData);

        std::vector<VkWriteDescriptorSet> writes;

        writes = {
            {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                frames_[i].blurVert, 0, 0, 1,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &blurBufferInfo, nullptr
            },
            {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                frames_[i].blurVert, 1, 0, 1,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &offscreenPass_.framebuffers[0].descriptor, nullptr, nullptr
            }
        };
        descriptorManager_.updateDescriptorSets(writes);

        writes = {
            {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                frames_[i].blurHorz, 0, 0, 1,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &blurBufferInfo, nullptr
            },
            {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                frames_[i].blurHorz, 1, 0, 1,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &offscreenPass_.framebuffers[1].descriptor, nullptr, nullptr
            }
        };
        descriptorManager_.updateDescriptorSets(writes);

        writes = {
            {
                VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                frames_[i].scene, 0, 0, 1,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &glowBufferInfo, nullptr
            }
        };
        descriptorManager_.updateDescriptorSets(writes);
    }
}

void Bloom::updateFrameDescriptor(uint32_t i) {
    if (i >= MAX_FRAMES_IN_FLIGHT) return;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

    // blurVert: sampler binding (binding 1) → fb[0]
    write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
             frames_[i].blurVert, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             &offscreenPass_.framebuffers[0].descriptor, nullptr, nullptr};
    vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);

    // blurHorz: sampler binding (binding 1) → fb[1]
    write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
             frames_[i].blurHorz, 1, 0, 1,
             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
             &offscreenPass_.framebuffers[1].descriptor, nullptr, nullptr};
    vkUpdateDescriptorSets(device_.device(), 1, &write, 0, nullptr);
}

std::unique_ptr<Pipeline> Bloom::createBlurPipeline(VkRenderPass renderPass, uint32_t blurdirection) {
    auto& vertCode = Bootstrapper::Get().getShader("resources/shaders/PostProcess/bloom/gaussblur.vert.spv");
    auto& fragCode = Bootstrapper::Get().getShader("resources/shaders/PostProcess/bloom/gaussblur.frag.spv");

    PipelineConfigInfo configInfo{};
    Pipeline::defaultPipelineConfigInfo(configInfo);

    configInfo.bindingDescriptions = {};
    configInfo.attributeDescriptions = {};

    configInfo.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    configInfo.blendAttachmentState.blendEnable = VK_TRUE;
    configInfo.blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
    configInfo.blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    configInfo.blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    configInfo.blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
    configInfo.blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    configInfo.blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;

    configInfo.depthStencilInfo.depthTestEnable = VK_FALSE;
    configInfo.depthStencilInfo.depthWriteEnable = VK_FALSE;

    configInfo.specMapEntries = {{0, 0, sizeof(uint32_t)}};
    configInfo.specData = {blurdirection};

    configInfo.renderPass = renderPass;
    configInfo.pipelineLayout = pipelineLayouts_.blur;

    return std::make_unique<Pipeline>(device_, vertCode, fragCode, configInfo);
}

void Bloom::createPipelines() {
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayouts_.blur;
    layoutInfo.pushConstantRangeCount = 0;
    layoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(device_.device(), &layoutInfo, nullptr, &pipelineLayouts_.blur) != VK_SUCCESS) {
        throw std::runtime_error("failed to create blur pipeline layout!");
    }

    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayouts_.scene;

    if (vkCreatePipelineLayout(device_.device(), &layoutInfo, nullptr, &pipelineLayouts_.scene) != VK_SUCCESS) {
        throw std::runtime_error("failed to create scene pipeline layout!");
    }

    // Create blur pipelines
    pipelines_.blurVert = createBlurPipeline(offscreenPass_.renderPass, 0);
    pipelines_.blurHorz = createBlurPipeline(activeCompositeRenderPass_, 1);

    // --- Color pass (glow objects) ---
    auto& vertCode = Bootstrapper::Get().getShader("resources/shaders/PostProcess/bloom/colorpass.vert.spv");
    auto& fragCode = Bootstrapper::Get().getShader("resources/shaders/PostProcess/bloom/colorpass.frag.spv");

    PipelineConfigInfo configInfo{};
    Pipeline::defaultPipelineConfigInfo(configInfo);

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(glm::vec3) * 2;
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attribDescs(2);
    attribDescs[0].binding = 0;
    attribDescs[0].location = 0;
    attribDescs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribDescs[0].offset = 0;
    attribDescs[1].binding = 0;
    attribDescs[1].location = 1;
    attribDescs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribDescs[1].offset = sizeof(glm::vec3);

    configInfo.bindingDescriptions = {bindingDesc};
    configInfo.attributeDescriptions = attribDescs;

    configInfo.blendAttachmentState.blendEnable = VK_FALSE;

    configInfo.depthStencilInfo.depthTestEnable = VK_FALSE;
    configInfo.depthStencilInfo.depthWriteEnable = VK_FALSE;

    configInfo.renderPass = offscreenPass_.renderPass;
    configInfo.pipelineLayout = pipelineLayouts_.scene;

    pipelines_.glowPass = std::make_unique<Pipeline>(device_, vertCode, fragCode, configInfo);
}

void Bloom::preScene(VkCommandBuffer cmd, uint32_t frameIndex,
                     const std::function<void(const FrameContext&)>& renderGlow,
                     const FrameContext& ctx) {
    beginGlowPass(cmd, frameIndex);

    FrameContext glowCtx = ctx;
    glowCtx.cmd = cmd;
    glowCtx.frameIndex = frameIndex;
    renderGlow(glowCtx);

    endGlowPass(cmd, frameIndex);
}

void Bloom::postScene(VkCommandBuffer cmd, uint32_t frameIndex,
                      const FrameContext& ctx) {
    compositeBloom(cmd, frameIndex, ctx.renderPass);
}

void Bloom::beginGlowPass(VkCommandBuffer cmd, uint32_t frameIndex) {
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = offscreenPass_.renderPass;
    rpBegin.framebuffer = offscreenPass_.framebuffers[0].framebuffer;
    rpBegin.renderArea.extent.width = offscreenPass_.width;
    rpBegin.renderArea.extent.height = offscreenPass_.height;
    rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
    rpBegin.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(offscreenPass_.width);
    viewport.height = static_cast<float>(offscreenPass_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent.width = offscreenPass_.width;
    scissor.extent.height = offscreenPass_.height;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayouts_.scene, 0, 1,
                            &frames_[frameIndex].scene, 0, nullptr);
    pipelines_.glowPass->bind(cmd);
}

void Bloom::endGlowPass(VkCommandBuffer cmd, uint32_t frameIndex) {
    vkCmdEndRenderPass(cmd);

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = offscreenPass_.renderPass;
    rpBegin.framebuffer = offscreenPass_.framebuffers[1].framebuffer;
    rpBegin.renderArea.extent.width = offscreenPass_.width;
    rpBegin.renderArea.extent.height = offscreenPass_.height;
    rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
    rpBegin.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(offscreenPass_.width);
    viewport.height = static_cast<float>(offscreenPass_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent.width = offscreenPass_.width;
    scissor.extent.height = offscreenPass_.height;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayouts_.blur, 0, 1,
                            &frames_[frameIndex].blurVert, 0, nullptr);
    pipelines_.blurVert->bind(cmd);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
}

void Bloom::compositeBloom(VkCommandBuffer cmd, uint32_t frameIndex, VkRenderPass activeRenderPass) {
    if (activeRenderPass != activeCompositeRenderPass_) {
        pipelines_.blurHorz.reset();
        pipelines_.blurHorz = createBlurPipeline(activeRenderPass, 1);
        activeCompositeRenderPass_ = activeRenderPass;
    }

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayouts_.blur, 0, 1,
                            &frames_[frameIndex].blurHorz, 0, nullptr);
    pipelines_.blurHorz->bind(cmd);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void Bloom::updateUniforms(uint32_t frameIndex, const GlowUniformData& glowData,
                           const BloomElement& element) {
    if (frameIndex >= MAX_FRAMES_IN_FLIGHT) return;

    frames_[frameIndex].glowUBO->write(&glowData, 0, sizeof(GlowUniformData));

    BlurUniformData blurData{};
    blurData.blurScale = element.radius;
    blurData.blurStrength = element.intensity;
    blurData.sigma = element.sigma;
    blurData.kernelSize = element.kernelSize;

    frames_[frameIndex].blurUBO->write(&blurData, 0, sizeof(BlurUniformData));
}

} // namespace lve
