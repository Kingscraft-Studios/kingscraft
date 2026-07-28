#pragma once
#include "FrameScene.hpp"
#include "Core/World/Chunk.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/Pipeline.hpp"
#include "Vulkan/TextureCache.hpp"

namespace lve {
    class WorldRenderer {
    public:
        WorldRenderer() = default;
        ~WorldRenderer();

        void init(Device& device, TextureCache& textureCache, VkRenderPass renderPass);
        void cleanup();

        void render(VkCommandBuffer cmd, const glm::mat4& viewProj, const FrameScene& scene);

        void createPipelineLayout();
        void createPipeline(bool disableTextures);

        void onRenderPassChanged(VkRenderPass renderPass);

    private:
        void drawChunk(VkCommandBuffer cmd, uint64_t chunkKey);

        Device* device_ = nullptr;
        TextureCache* textureCache_ = nullptr;
        VkRenderPass renderPass_ = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
        std::unique_ptr<Pipeline> pipeline_;

        bool lastDisableTextures_ = false;
    };
}
