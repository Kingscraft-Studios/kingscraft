#pragma once

#include "Core/FrameContext.hpp"
#include "Core/World/Chunk.hpp"
#include "Vulkan/Pipeline.hpp"
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace lve {

    class Device;
    class TextureCache;

    class TerrainRenderer {
    public:
        TerrainRenderer() = default;
        ~TerrainRenderer();

        void init(Device& device, TextureCache& textureCache, VkRenderPass renderPass);
        void cleanup();

        void render(VkCommandBuffer cmd, const std::vector<Chunk*>& chunks,
                    const glm::mat4& viewProj, const glm::vec3& cameraPos,
                    bool enableFrustumCulling, float worldHeight,
                    double* outFrustumMs = nullptr,
                    double* outDrawMs = nullptr,
                    uint32_t* outVisibleChunks = nullptr,
                    uint32_t* outVisibleSubChunks = nullptr);

        void onRenderPassChanged(VkRenderPass renderPass);

    private:
        void createPipelineLayout(TextureCache& textureCache);
        void createPipeline(bool disableTextures);

        Device* device_ = nullptr;
        TextureCache* textureCache_ = nullptr;
        VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
        VkRenderPass renderPass_ = VK_NULL_HANDLE;
        std::unique_ptr<Pipeline> pipeline_;
        bool lastDisableTextures_ = false;
    };

} // namespace lve
