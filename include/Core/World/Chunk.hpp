#pragma once

#include "Vulkan/Device.hpp"
#include "Vulkan/Buffer.hpp"
#include <vector>
#include <memory>
#include <array>
#include <glm/glm.hpp>

namespace lve {

    static constexpr uint32_t SUBCHUNK_H = 4;

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

    struct SubChunk {
        int yBase = 0;
        std::vector<ChunkVertex> vertices;
        std::vector<uint16_t> indices;
        uint32_t indexCount = 0;
        bool meshNeeded = true;
    };

    class Chunk {
    public:
        Chunk(glm::ivec2 gridPos, int verticesPerAxis, float spacing, int height);

        glm::ivec2 getGridPos() const { return gridPos_; }
        glm::vec3 getWorldOrigin() const { return worldOrigin_; }
        int getVerticesPerAxis() const { return verticesPerAxis_; }

        std::vector<SubChunk>& getSubChunks() { return subChunks_; }
        const std::vector<SubChunk>& getSubChunks() const { return subChunks_; }

        const std::vector<uint8_t>& getBlockData() const { return blockData_; }
        void setBlockData(std::vector<uint8_t> data, int chunkSize, int height);
        uint8_t getBlock(int x, int y, int z) const;
        void setBlock(int x, int y, int z, uint8_t blockId);

        int getBlockDataSize() const { return chunkSize_; }
        int getHeight() const { return height_; }

        uint16_t getHeightAt(int localX, int localZ) const {
            return heightMap_[localZ * chunkSize_ + localX];
        }
        uint16_t getMaxHeight() const { return maxHeight_; }
        uint16_t getMinHeight() const { return minHeight_; }
        const std::vector<uint16_t>& getHeightMap() const { return heightMap_; }

        bool isRemeshNeeded() const;
        void markDirty();
        void markRemeshed();

        void upload();

    private:
        glm::ivec2 gridPos_;
        glm::vec3 worldOrigin_;
        int verticesPerAxis_;
        float spacing_;

        std::vector<uint8_t> blockData_;
        int chunkSize_ = 0;
        int height_ = 0;

        std::vector<SubChunk> subChunks_;

        std::vector<uint16_t> heightMap_;
        uint16_t maxHeight_ = 0;
        uint16_t minHeight_ = 0;

        uint64_t lastMeshHash_ = 0;
        bool hasUploaded_ = false;

        void rebuildHeightmap();
        void updateMinMaxHeight();

        static uint64_t hashBytes(const uint8_t* data, size_t size);
    };

} // namespace lve
