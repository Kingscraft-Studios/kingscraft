#pragma once

#include "Vulkan/Device.hpp"
#include "Vulkan/DescriptorManager.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/Pipeline.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/FallbackTexture.hpp"
#include "Vulkan/DescriptorSetLayout.hpp"
#include "Vulkan/DescriptorPool.hpp"
#include "UI/Engine/UiBatchQueue.hpp"
#include "UiStyle.hpp"
#include "Core/Constants.hpp"
#include <array>
#include <memory>
#include <glm/glm.hpp>

namespace kc {

    class UiRenderer {
    public:
        static constexpr uint32_t MAX_STYLES = 64;
        static constexpr uint32_t MAX_ELEMENTS = 1024;

        UiRenderer(Device& device, DescriptorManager& descriptorManager, VkExtent2D extent);
        ~UiRenderer();

        UiRenderer(const UiRenderer&) = delete;
        UiRenderer& operator=(const UiRenderer&) = delete;

        void init();
        void shutdown();
        void resize(VkExtent2D extent);

        // Draws the batched UI straight into the currently bound render pass
        // (the world/menu target). No offscreen image, no fullscreen composite.
        void render(VkCommandBuffer cmd, UiBatchQueue& batchQueue, uint32_t frameIndex);
        void setTargetRenderPass(VkRenderPass renderPass);

        void setFontAtlas(VkImageView imageView, VkSampler sampler);
        bool hasFontAtlas() const { return atlasView_ != VK_NULL_HANDLE; }

        void setBlockTexture(VkImageView imageView, VkSampler sampler);
        bool hasBlockTexture() const { return blockTexView_ != VK_NULL_HANDLE; }

        void uploadStylePool(const void* data, uint32_t count);
        void uploadElementStyles(const void* data, uint32_t firstElement, uint32_t count);

    private:
        void createPipeline();
        void createDescriptorSetLayouts();
        void createDescriptorPools();
        void allocateDescriptorSets();
        void createUniformBuffer();
        void updateUniformBuffer();
        void updateUiDescriptorSet(uint32_t frameIndex);

        Device& device_;
        DescriptorManager& descriptorManager_;
        VkExtent2D extent_;
        bool initialized_ = false;

        // Render pass the UI pipeline is built against. The pipeline is created
        // lazily on first use and rebuilt whenever the target pass changes.
        VkRenderPass targetRenderPass_ = VK_NULL_HANDLE;

        // Pipelines
        std::unique_ptr<Pipeline> uiPipeline_;
        std::unique_ptr<PipelineLayout> pipelineLayout_;

        // Descriptor set layouts
        std::unique_ptr<DescriptorSetLayout> uiDescriptorSetLayout_;

        // Descriptor pools
        std::unique_ptr<DescriptorPool> uiDescriptorPool_;

        // Descriptor sets (per frame-in-flight)
        std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> uiDescriptorSets_{};

        // Uniform buffer (binding 0)
        std::unique_ptr<Buffer> uniformBuffer_;

        // Font atlas (binding 1)
        VkImageView atlasView_ = VK_NULL_HANDLE;
        VkSampler atlasSampler_ = VK_NULL_HANDLE;
        bool atlasDirty_ = false;
        uint32_t uiDescDirtyMask_ = 0;

        // Deferred pipeline retirement
        struct RetiredPipeline {
            std::unique_ptr<Pipeline> pipeline;
            std::unique_ptr<PipelineLayout> layout;
        };
        std::array<std::vector<RetiredPipeline>, MAX_FRAMES_IN_FLIGHT> retiredPipelines_;
        uint32_t currentFrameIndex_ = 0;

        // Style pool SSBO (binding 2) — stores GpuStyle entries
        std::unique_ptr<Buffer> stylePoolBuffer_;
        VkDescriptorBufferInfo stylePoolInfo_{};

        // Element→style index SSBO (binding 3) — maps elementId to styleIndex
        std::unique_ptr<Buffer> elementStylesBuffer_;
        VkDescriptorBufferInfo elementStylesInfo_{};

        // Block texture array (set 1, binding 0)
        std::unique_ptr<DescriptorSetLayout> blockTexLayout_;
        std::unique_ptr<DescriptorPool> blockTexPool_;
        std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> blockTexSets_{};
        VkImageView blockTexView_ = VK_NULL_HANDLE;
        VkSampler blockTexSampler_ = VK_NULL_HANDLE;
        bool blockTexDirty_ = false;

        // Placeholder bound as the block texture until setBlockTexture() supplies
        // the real atlas.
        std::unique_ptr<FallbackTexture> fallbackTexture_;
    };

} // namespace kc
