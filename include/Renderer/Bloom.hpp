#pragma once

#include "Renderer/PostProcessing.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/Pipeline.hpp"
#include "Vulkan/DescriptorManager.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/Framebuffer.hpp"
#include "Vulkan/Sampler.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/DescriptorSetLayout.hpp"
#include "Vulkan/DescriptorPool.hpp"
#include "Core/Constants.hpp"
#include <array>
#include <memory>
#include <glm/glm.hpp>

namespace lve {

struct BloomElement {
    bool bloomEnabled = true;
    float luminosity = 1.0f;
    glm::vec3 emissiveColor = glm::vec3(0.0f, 1.0f, 1.0f);
    float radius = 1.0f;
    float intensity = 2.0f;
    float sigma = 3.0f;
    int kernelSize = 11;
};

struct BlurUniformData {
    float blurScale;
    float blurStrength;
    float sigma;
    int kernelSize;
};

struct GlowUniformData {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model;
};

class Bloom : public PostProcessEffect {
public:
    static constexpr uint32_t MAX_FB_DIM = 256;
    static constexpr VkFormat FB_COLOR_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

    struct FrameBufferAttachment {
        std::unique_ptr<Image> image;
        std::unique_ptr<ImageView> view;
    };

    struct FrameBuffer {
        std::unique_ptr<Framebuffer> framebuffer;
        FrameBufferAttachment color, depth;
        VkDescriptorImageInfo descriptor{};
    };

    struct OffscreenPass {
        int32_t width = MAX_FB_DIM, height = MAX_FB_DIM;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        std::unique_ptr<Sampler> sampler;
        std::array<FrameBuffer, 2> framebuffers;
    };

    Bloom(Device& device, VkExtent2D windowExtent, VkRenderPass sceneRenderPass,
          DescriptorManager& descriptorManager);
    ~Bloom() override;

    Bloom(const Bloom&) = delete;
    Bloom& operator=(const Bloom&) = delete;

    const char* name() const override { return "bloom"; }

    void preScene(VkCommandBuffer cmd, uint32_t frameIndex,
                  const std::function<void(const FrameContext&)>& renderGlow,
                  const FrameContext& ctx) override;

    void postScene(VkCommandBuffer cmd, uint32_t frameIndex,
                   const FrameContext& ctx) override;

    void recreate(VkExtent2D windowExtent, VkRenderPass sceneRenderPass);

    void updateUniforms(uint32_t frameIndex, const GlowUniformData& glowData,
                        const BloomElement& element);

    VkRenderPass getOffscreenRenderPass() const { return offscreenPass_.renderPass; }
    VkPipelineLayout getSceneLayout() const { return pipelineLayouts_.scene ? pipelineLayouts_.scene->getHandle() : VK_NULL_HANDLE; }
    VkPipeline getGlowPipeline() const { return pipelines_.glowPass ? pipelines_.glowPass->getHandle() : VK_NULL_HANDLE; }

    static void computeOffscreenDim(VkExtent2D windowExtent, int32_t& outW, int32_t& outH);

private:
    void beginGlowPass(VkCommandBuffer cmd, uint32_t frameIndex);
    void endGlowPass(VkCommandBuffer cmd, uint32_t frameIndex);
    void compositeBloom(VkCommandBuffer cmd, uint32_t frameIndex, VkRenderPass activeRenderPass);

    void destroyFramebuffer(FrameBuffer& fb);
    void createOffscreenFramebuffer(FrameBuffer* fb, VkFormat colorFormat, VkFormat depthFormat);
    void createOffscreen();
    void destroyOffscreenFramebuffers();
    void createDescriptors();
    std::unique_ptr<Pipeline> createBlurPipeline(VkRenderPass renderPass, uint32_t blurdirection);
    void createPipelines();
    void createUniformBuffers();
    void updateFrameDescriptor(uint32_t i);

    Device& device_;
    DescriptorManager& descriptorManager_;
    VkRenderPass activeCompositeRenderPass_ = VK_NULL_HANDLE;
    VkExtent2D windowExtent_{};

    OffscreenPass offscreenPass_;

    struct {
        std::unique_ptr<PipelineLayout> blur;
        std::unique_ptr<PipelineLayout> scene;
    } pipelineLayouts_;

    struct {
        std::unique_ptr<Pipeline> blurVert;
        std::unique_ptr<Pipeline> blurHorz;
        std::unique_ptr<Pipeline> glowPass;
    } pipelines_;

    std::unique_ptr<DescriptorSetLayout> blurDescriptorSetLayout_;
    std::unique_ptr<DescriptorSetLayout> sceneDescriptorSetLayout_;

    struct PerFrame {
        VkDescriptorSet blurVert = VK_NULL_HANDLE;
        VkDescriptorSet blurHorz = VK_NULL_HANDLE;
        VkDescriptorSet scene = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> glowUBO;
        std::unique_ptr<Buffer> blurUBO;
    };
    std::array<PerFrame, MAX_FRAMES_IN_FLIGHT> frames_;

    std::unique_ptr<DescriptorPool> descriptorPool_;

    std::unique_ptr<RenderPass> offscreenRenderPass_;

    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
};

} // namespace lve
