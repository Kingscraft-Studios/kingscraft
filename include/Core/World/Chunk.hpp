#pragma once

#include "Vulkan/Device.hpp"
#include "Vulkan/Buffer.hpp"
#include <vector>
#include <memory>
#include <array>
#include <glm/glm.hpp>

namespace lve {

    struct ChunkVertex {
        glm::vec3 position;
        glm::vec2 uv;
        float texIndex;

        static VkVertexInputBindingDescription getBindingDescription() {
            VkVertexInputBindingDescription desc{};
            desc.binding = 0;
            desc.stride = sizeof(ChunkVertex);
            desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return desc;
        }

        static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
            std::array<VkVertexInputAttributeDescription, 3> desc{};
            desc[0].binding = 0;
            desc[0].location = 0;
            desc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            desc[0].offset = offsetof(ChunkVertex, position);
            desc[1].binding = 0;
            desc[1].location = 1;
            desc[1].format = VK_FORMAT_R32G32_SFLOAT;
            desc[1].offset = offsetof(ChunkVertex, uv);
            desc[2].binding = 0;
            desc[2].location = 2;
            desc[2].format = VK_FORMAT_R32_SFLOAT;
            desc[2].offset = offsetof(ChunkVertex, texIndex);
            return desc;
        }
    };

    class Chunk {
    public:
        Chunk(Device& device, glm::ivec2 gridPos, int verticesPerAxis, float spacing);
        ~Chunk();

        glm::ivec2 getGridPos() const { return gridPos_; }
        glm::vec3 getWorldOrigin() const { return worldOrigin_; }
        int getVerticesPerAxis() const { return verticesPerAxis_; }

        std::vector<ChunkVertex>& vertices() { return vertices_; }
        std::vector<uint16_t>& indices() { return indices_; }
        uint32_t getIndexCount() const { return indexCount_; }

        void upload();
        void bindAndDraw(VkCommandBuffer cmd);
        void cleanup();

    private:
        Device& device_;
        glm::ivec2 gridPos_;
        glm::vec3 worldOrigin_;
        int verticesPerAxis_;
        float spacing_;

        std::vector<ChunkVertex> vertices_;
        std::vector<uint16_t> indices_;
        uint32_t indexCount_ = 0;

        std::unique_ptr<Buffer> vertexBuffer_;
        std::unique_ptr<Buffer> indexBuffer_;

        VkFence uploadCompleteFence_ = VK_NULL_HANDLE;
        VkCommandBuffer uploadCmd_ = VK_NULL_HANDLE;
    };

} // namespace lve
