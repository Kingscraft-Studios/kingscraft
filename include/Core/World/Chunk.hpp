#pragma once

#include "Vulkan/Device.hpp"
#include "Vulkan/Buffer.hpp"
#include <vector>
#include <memory>
#include <array>
#include <glm/glm.hpp>

namespace lve {

    struct ChunkVertex {
        uint8_t  px;       // offset 0 — local chunk coordinate
        uint8_t  py;       // offset 1
        uint8_t  pz;       // offset 2
        uint8_t  face;     // offset 3 — 0=PosY,1=NegY,2=PosZ,3=NegZ,4=PosX,5=NegX
        int8_t   uvX;      // offset 4 — integer UV
        int8_t   uvY;      // offset 5
        uint16_t texIndex; // offset 6 — texture array layer index

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
            desc[0].format = VK_FORMAT_R8G8B8A8_UINT;
            desc[0].offset = offsetof(ChunkVertex, px);
            desc[1].binding = 0;
            desc[1].location = 1;
            desc[1].format = VK_FORMAT_R8G8_SINT;
            desc[1].offset = offsetof(ChunkVertex, uvX);
            desc[2].binding = 0;
            desc[2].location = 2;
            desc[2].format = VK_FORMAT_R16_UINT;
            desc[2].offset = offsetof(ChunkVertex, texIndex);
            return desc;
        }
    };

    static_assert(sizeof(ChunkVertex) == 8, "ChunkVertex must be 8 bytes");

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

        const std::vector<uint8_t>& getBlockData() const { return blockData_; }
        void setBlockData(std::vector<uint8_t> data, int chunkSize, int height);
        uint8_t getBlock(int x, int y, int z) const;
        void setBlock(int x, int y, int z, uint8_t blockId);

        int getBlockDataSize() const { return chunkSize_; }
        int getHeight() const { return height_; }

        bool isRemeshNeeded() const { return remeshNeeded_; }
        void markDirty() { remeshNeeded_ = true; }
        void markRemeshed() { remeshNeeded_ = false; }

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

        std::vector<uint8_t> blockData_;
        int chunkSize_ = 0;
        int height_ = 0;
        bool remeshNeeded_ = false;

        std::unique_ptr<Buffer> vertexBuffer_;
        std::unique_ptr<Buffer> indexBuffer_;
        std::unique_ptr<Buffer> prevVertexBuffer_;
        std::unique_ptr<Buffer> prevIndexBuffer_;

        VkFence uploadCompleteFence_ = VK_NULL_HANDLE;
        VkCommandBuffer uploadCmd_ = VK_NULL_HANDLE;
    };

} // namespace lve
