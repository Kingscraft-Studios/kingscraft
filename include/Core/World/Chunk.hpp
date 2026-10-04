#pragma once

#include "Vulkan/Device.hpp"
#include "Vulkan/Buffer.hpp"
#include <vector>
#include <memory>
#include <array>
#include <glm/glm.hpp>

namespace kc {

    static constexpr uint32_t SUBCHUNK_H = 4;
    static constexpr int MAX_SUBCHUNKS = 128;

    // One bit per sub-chunk. A full-height column is 384 / SUBCHUNK_H = 96
    // sub-chunks, so a 32-bit mask is both too small and undefined to shift past
    // bit 31; 128 bits covers the height cap.
    static_assert(MAX_SUBCHUNKS * static_cast<int>(SUBCHUNK_H) >= 384,
                  "MAX_SUBCHUNKS must cover a full-height world");

    struct SubChunkMask {
        std::array<uint64_t, MAX_SUBCHUNKS / 64> words{};

        void set(size_t i) {
            if (i < static_cast<size_t>(MAX_SUBCHUNKS)) words[i >> 6] |= (1ull << (i & 63));
        }
        bool test(size_t i) const {
            return i < static_cast<size_t>(MAX_SUBCHUNKS) &&
                   (words[i >> 6] & (1ull << (i & 63))) != 0;
        }
        bool empty() const {
            for (uint64_t w : words) if (w) return false;
            return true;
        }
        void merge(const SubChunkMask& other) {
            for (size_t w = 0; w < words.size(); ++w) words[w] |= other.words[w];
        }
    };

    // Per-vertex chunk-local position + face + tile. py is a signed 16-bit
    // chunk-local Y (0..worldHeight-1); the shader adds the chunk's world
    // origin, whose Y is RendererSettings::minY, to reach world space. It has
    // to be 16-bit because a full Minecraft-height column no longer fits in a
    // uint8_t. Field order keeps every attribute naturally aligned so the
    // 10-byte stride needs no vertex-buffer alignment padding.
    struct ChunkVertex {
        uint8_t  px;       // offset 0 — local chunk coordinate
        uint8_t  pz;       // offset 1
        int16_t  py;       // offset 2 — chunk-local Y, 0..383
        uint8_t  face;     // offset 4 — 0=PosY,1=NegY,2=PosZ,3=NegZ,4=PosX,5=NegX
        int8_t   uvX;      // offset 5 — integer UV
        int8_t   uvY;      // offset 6
        uint8_t  _pad;     // offset 7 — explicit tail padding
        uint16_t texIndex; // offset 8 — texture array layer index

        static VkVertexInputBindingDescription getBindingDescription() {
            VkVertexInputBindingDescription desc{};
            desc.binding = 0;
            desc.stride = sizeof(ChunkVertex);
            desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return desc;
        }

        static std::array<VkVertexInputAttributeDescription, 5> getAttributeDescriptions() {
            std::array<VkVertexInputAttributeDescription, 5> desc{};
            desc[0].binding = 0;
            desc[0].location = 0;
            desc[0].format = VK_FORMAT_R8G8_UINT;      // px, pz
            desc[0].offset = offsetof(ChunkVertex, px);
            desc[1].binding = 0;
            desc[1].location = 1;
            desc[1].format = VK_FORMAT_R16_SINT;       // py
            desc[1].offset = offsetof(ChunkVertex, py);
            desc[2].binding = 0;
            desc[2].location = 2;
            desc[2].format = VK_FORMAT_R8_UINT;        // face
            desc[2].offset = offsetof(ChunkVertex, face);
            desc[3].binding = 0;
            desc[3].location = 3;
            desc[3].format = VK_FORMAT_R8G8_SINT;      // uvX, uvY
            desc[3].offset = offsetof(ChunkVertex, uvX);
            desc[4].binding = 0;
            desc[4].location = 4;
            desc[4].format = VK_FORMAT_R16_UINT;       // texIndex
            desc[4].offset = offsetof(ChunkVertex, texIndex);
            return desc;
        }
    };

    static_assert(sizeof(ChunkVertex) == 10, "ChunkVertex must be 10 bytes");
    static_assert(offsetof(ChunkVertex, py) == 2, "py must be 2-byte aligned");
    static_assert(offsetof(ChunkVertex, texIndex) == 8, "texIndex must be 2-byte aligned");

    struct SubChunk {
        int yBase = 0;
        std::vector<ChunkVertex> vertices;
        std::vector<uint16_t> indices;
        uint32_t indexCount = 0;
        std::vector<ChunkVertex> transparentVertices;
        std::vector<uint16_t> transparentIndices;
        uint32_t transparentIndexCount = 0;
        bool meshNeeded = true;
    };

    // Immutable block grid, shared between blockCache_ and live chunks. Mesh
    // tasks grab the handle (no copy); block edits copy-on-write. Each cell
    // holds the encoded uint64 identifier (0 = air/empty).
    using BlockDataPtr = std::shared_ptr<const std::vector<uint64_t>>;

    // Per-block classification bits resolved once per heightmap rebuild, so the
    // 384-deep column scan never touches the block registry.
    struct BlockFlags {
        bool nonAir       = false;  // anything but air
        bool motionBlocks = false;  // stops an entity walking through
        bool liquid       = false;  // carries a fluid state
        bool oceanFloor   = false;  // counts as seabed rather than ocean fill
        bool transparent  = false;
    };

    class Chunk {
    public:
        Chunk(glm::ivec2 gridPos, int verticesPerAxis, float spacing, int height);

        glm::ivec2 getGridPos() const { return gridPos_; }
        glm::vec3 getWorldOrigin() const { return worldOrigin_; }
        int getVerticesPerAxis() const { return verticesPerAxis_; }

        std::vector<SubChunk>& getSubChunks() { return subChunks_; }
        const std::vector<SubChunk>& getSubChunks() const { return subChunks_; }

        const std::vector<uint64_t>& getBlockData() const {
            static const std::vector<uint64_t> empty;
            return blockData_ ? *blockData_ : empty;
        }
        BlockDataPtr getBlockDataPtr() const { return blockData_; }
        void setBlockData(BlockDataPtr data, int chunkSize, int height);
        uint64_t getBlock(int x, int y, int z) const;
        void setBlock(int x, int y, int z, uint64_t blockId);

        int getBlockDataSize() const { return chunkSize_; }
        int getHeight() const { return height_; }

        // World Y of this chunk's local Y 0. Local Y is 0-based inside the
        // chunk; only the World/player boundary converts.
        int getMinY() const { return minY_; }
        int getMaxY() const { return minY_ + height_ - 1; }
        int worldToLocalY(int worldY) const { return worldY - minY_; }
        int localToWorldY(int localY) const { return localY + minY_; }

        // Heightmaps mirror vanilla Heightmap.Types, including its storage
        // convention: a heightmap holds the "first available" Y, i.e. one
        // above the topmost matching block, so an empty column reads 0 rather
        // than colliding with a column whose lowest block is at local Y 0.
        // Predicates follow vanilla:
        //   WORLD_SURFACE   = not air
        //   OCEAN_FLOOR     = motion blocking        (water is ocean fill, not floor)
        //   MOTION_BLOCKING = motion blocking or liquid
        // Values are chunk-local; add minY_ for world space.
        uint16_t getWorldSurfaceFirstAvailable(int localX, int localZ) const {
            return worldSurfaceHeightMap_[static_cast<size_t>(localZ) * chunkSize_ + localX];
        }
        uint16_t getMotionBlockingFirstAvailable(int localX, int localZ) const {
            return motionBlockingHeightMap_[static_cast<size_t>(localZ) * chunkSize_ + localX];
        }
        uint16_t getOceanFloorFirstAvailable(int localX, int localZ) const {
            return oceanFloorHeightMap_[static_cast<size_t>(localZ) * chunkSize_ + localX];
        }

        // Same three maps as topmost *block* Y, -1 when the column is empty.
        int getWorldSurfaceTopY(int localX, int localZ) const {
            return static_cast<int>(getWorldSurfaceFirstAvailable(localX, localZ)) - 1;
        }
        int getMotionBlockingTopY(int localX, int localZ) const {
            return static_cast<int>(getMotionBlockingFirstAvailable(localX, localZ)) - 1;
        }
        int getOceanFloorTopY(int localX, int localZ) const {
            return static_cast<int>(getOceanFloorFirstAvailable(localX, localZ)) - 1;
        }

        // Vertical extremes of every non-air cell, used for frustum culling and
        // for bounding the mesh job range.
        uint16_t getMaxHeight() const { return maxHeight_; }
        uint16_t getMinHeight() const { return minHeight_; }

        // Sub-chunks worth meshing. A sub-chunk holding only air produces no
        // geometry, so it is never handed to the mesher. Occupancy bit 0 = has
        // non-air, bit 1 = has transparent (geometry may exist either way).
        bool isSubChunkOccupied(size_t subIndex) const {
            return subIndex < subOccupancy_.size() && (subOccupancy_[subIndex] & 1u) != 0;
        }
        bool isSubChunkTransparent(size_t subIndex) const {
            return subIndex < subOccupancy_.size() && (subOccupancy_[subIndex] & 2u) != 0;
        }
        // Half-open [first, last] sub-chunk index range that can contain
        // geometry. Empty chunks report an empty range.
        std::pair<size_t, size_t> occupiedSubChunkRange() const {
            if (subChunks_.empty() || !hasOccupiedSubChunks_) return {0, 0};
            return {firstOccupiedSub_, lastOccupiedSub_ + 1};
        }

        bool isRemeshNeeded() const;
        void markDirty();
        void markRemeshed();

        void upload();

    private:
        glm::ivec2 gridPos_;
        glm::vec3 worldOrigin_;
        int verticesPerAxis_;
        float spacing_;

        std::vector<SubChunk> subChunks_;

        BlockDataPtr blockData_;
        int chunkSize_ = 0;
        int height_ = 0;
        int minY_ = 0;

        std::vector<uint16_t> worldSurfaceHeightMap_;
        std::vector<uint16_t> motionBlockingHeightMap_;
        std::vector<uint16_t> oceanFloorHeightMap_;
        uint16_t maxHeight_ = 0;
        uint16_t minHeight_ = 0;

        std::vector<uint8_t> subOccupancy_;
        size_t firstOccupiedSub_ = 0;
        size_t lastOccupiedSub_ = 0;
        bool hasOccupiedSubChunks_ = false;

        uint64_t lastMeshHash_ = 0;
        bool hasUploaded_ = false;

        void rebuildHeightMaps();
        // Recomputes all heightmaps, occupancy and the vertical extremes of one
        // column after a block edit.
        void refreshColumn(int localX, int localZ, const std::vector<uint64_t>& data);
        void updateOccupancy(const std::vector<uint64_t>& data);

        static uint64_t hashBytes(const uint8_t* data, size_t size);
    };

} // namespace kc
