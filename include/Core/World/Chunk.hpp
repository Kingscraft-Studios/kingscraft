#pragma once

#include "Vulkan/Device.hpp"
#include "Vulkan/Buffer.hpp"
#include "Core/World/BlockGrid.hpp"
#include "Core/World/Location.hpp"
#include <vector>
#include <memory>
#include <array>
#include <unordered_map>
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

    // Per-vertex chunk-local position + face + tile, packed into two 32-bit
    // words (8 bytes) so the vertex fetch reads two attributes instead of five.
    // Field ranges are what the mesher can actually emit:
    //   px / pz  0..16   (chunkSize, one extra for the far plane of a chunk)
    //   py       0..384  (chunk-local Y; a top face of the highest block is 384)
    //   face     0..5
    //   uvX/uvY  0..16   (greedy-mesh tile repeat)
    // The vertex shader unpacks this word; the code below keeps the C++ side in
    // one place so the bit layout cannot drift from resources/shaders/terrain.vert.
    struct ChunkVertex {
        static constexpr uint32_t PX_SHIFT  = 0,  PX_BITS  = 5;
        static constexpr uint32_t PZ_SHIFT  = 5,  PZ_BITS  = 5;
        static constexpr uint32_t PY_SHIFT  = 10, PY_BITS  = 9;
        static constexpr uint32_t FACE_SHIFT = 19, FACE_BITS = 3;
        static constexpr uint32_t UVX_SHIFT = 22, UVX_BITS = 5;
        static constexpr uint32_t UVY_SHIFT = 27, UVY_BITS = 5;

        static constexpr uint32_t PX_MASK   = (1u << PX_BITS) - 1u;
        static constexpr uint32_t PZ_MASK   = (1u << PZ_BITS) - 1u;
        static constexpr uint32_t PY_MASK   = (1u << PY_BITS) - 1u;
        static constexpr uint32_t FACE_MASK = (1u << FACE_BITS) - 1u;
        static constexpr uint32_t UVX_MASK  = (1u << UVX_BITS) - 1u;
        static constexpr uint32_t UVY_MASK  = (1u << UVY_BITS) - 1u;

        static_assert(PX_SHIFT + PX_BITS == PZ_SHIFT, "packed vertex bits overlap");
        static_assert(PZ_SHIFT + PZ_BITS == PY_SHIFT, "packed vertex bits overlap");
        static_assert(PY_SHIFT + PY_BITS == FACE_SHIFT, "packed vertex bits overlap");
        static_assert(FACE_SHIFT + FACE_BITS == UVX_SHIFT, "packed vertex bits overlap");
        static_assert(UVX_SHIFT + UVX_BITS == UVY_SHIFT, "packed vertex bits overlap");
        static_assert(UVY_SHIFT + UVY_BITS == 32, "packed vertex word must be full");

        uint32_t packed = 0;    // position, face and uv
        uint32_t texIndex = 0;  // texture array layer index

        static ChunkVertex make(uint32_t px, uint32_t py, uint32_t pz, uint32_t face,
                                int uvX, int uvY, uint32_t texIndex) {
            ChunkVertex v;
            v.packed = ((px & PX_MASK) << PX_SHIFT)
                     | ((pz & PZ_MASK) << PZ_SHIFT)
                     | ((py & PY_MASK) << PY_SHIFT)
                     | ((face & FACE_MASK) << FACE_SHIFT)
                     | ((static_cast<uint32_t>(uvX) & UVX_MASK) << UVX_SHIFT)
                     | ((static_cast<uint32_t>(uvY) & UVY_MASK) << UVY_SHIFT);
            v.texIndex = texIndex;
            return v;
        }

        uint32_t px() const { return (packed >> PX_SHIFT) & PX_MASK; }
        uint32_t pz() const { return (packed >> PZ_SHIFT) & PZ_MASK; }
        uint32_t py() const { return (packed >> PY_SHIFT) & PY_MASK; }
        uint32_t face() const { return (packed >> FACE_SHIFT) & FACE_MASK; }
        uint32_t uvX() const { return (packed >> UVX_SHIFT) & UVX_MASK; }
        uint32_t uvY() const { return (packed >> UVY_SHIFT) & UVY_MASK; }

        static VkVertexInputBindingDescription getBindingDescription() {
            VkVertexInputBindingDescription desc{};
            desc.binding = 0;
            desc.stride = sizeof(ChunkVertex);
            desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return desc;
        }

        static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() {
            std::array<VkVertexInputAttributeDescription, 2> desc{};
            desc[0].binding = 0;
            desc[0].location = 0;
            desc[0].format = VK_FORMAT_R32_UINT;       // packed position/face/uv
            desc[0].offset = offsetof(ChunkVertex, packed);
            desc[1].binding = 0;
            desc[1].location = 1;
            desc[1].format = VK_FORMAT_R32_UINT;       // texture array layer index
            desc[1].offset = offsetof(ChunkVertex, texIndex);
            return desc;
        }
    };

    static_assert(sizeof(ChunkVertex) == 8, "ChunkVertex must be 8 bytes");
    static_assert(offsetof(ChunkVertex, texIndex) == 4, "texIndex must be 4-byte aligned");

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

    // Block grid storage (Phase 1: hybrid dense BlockGrid). Mesh tasks grab
    // the handle (no copy); block edits copy-on-write.
    using BlockDataPtr = BlockGridPtr;

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
            if (blockGrid_) return blockGrid_->getDenseData();
            return empty;
        }
        BlockGridPtr getBlockDataPtr() const { return blockGrid_; }
        BlockGridPtr getBlockGrid() const { return blockGrid_; }
        void setBlockData(BlockGridPtr grid, int chunkSize, int height);
        void setBaseGrid(BlockGridPtr grid) { baseGrid_ = grid; }
        BlockGridPtr getBaseGrid() const { return baseGrid_; }
        uint64_t getBlock(const Location& loc) const;
        void setBlock(const Location& loc, uint64_t blockId);
        void setBlockPlayer(const Location& loc, uint64_t blockId);
        void applyPlayerOverridesFromMap(const std::unordered_map<Location, uint64_t>& overrides);
        const std::unordered_map<Location, uint64_t>& getPlayerOverrides() const { return playerOverrides_; }
        std::unordered_map<Location, uint64_t>& getPlayerOverrides() { return playerOverrides_; }
        bool hasPlayerOverrides() const { return !playerOverrides_.empty(); }
        void setPlayerOverrides(std::unordered_map<Location, uint64_t> overrides) { playerOverrides_ = std::move(overrides); }

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

        BlockGridPtr blockGrid_;
        BlockGridPtr baseGrid_;
        std::unordered_map<Location, uint64_t> playerOverrides_;
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
        void refreshColumn(int localX, int localZ, const BlockGrid& grid);
        void updateOccupancy(const BlockGrid& grid);

        static uint64_t hashBytes(const uint8_t* data, size_t size);
    };

} // namespace kc
