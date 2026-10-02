#include "Core/World/Chunk.hpp"

#include "Bus/MessageBus.hpp"
#include "Core/Runtime.hpp"
#include "Core/World/ChunkKey.hpp"
#include "Core/World/ChunkUploadData.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Threads/RenderThread.hpp"
#include <algorithm>

namespace kc {

    namespace {

        // Dense id -> flag table rebuilt per heightmap pass. The registry holds
        // a handful of blocks, so a linear probe over contiguous arrays beats a
        // hash lookup, and air (the overwhelming majority of the 384-deep
        // column) short-circuits on the encoded id alone.
        class BlockFlagTable {
        public:
            void build() {
                ids_.clear();
                flags_.clear();
                Registry<Block>::getRegistry().forEach([this](uint64_t encoded, const Block& block) {
                    BlockFlags f;
                    f.nonAir = true;
                    f.motionBlocks = block.isSolid();
                    f.liquid = block.isLiquid();
                    f.oceanFloor = block.isSolid();
                    f.transparent = block.isTransparent();
                    ids_.push_back(encoded);
                    flags_.push_back(f);
                });
                // Vanilla MOTION_BLOCKING accepts motion blockers *or* anything
                // with a fluid state; in this engine that means a liquid.
                for (auto& f : flags_)
                    f.motionBlocks = f.motionBlocks || f.liquid;
            }

            const BlockFlags& lookup(uint64_t id) const {
                static const BlockFlags kAir{};
                if (id == 0) return kAir;
                for (size_t i = 0; i < ids_.size(); ++i)
                    if (ids_[i] == id) return flags_[i];
                return kAir;
            }

        private:
            std::vector<uint64_t> ids_;
            std::vector<BlockFlags> flags_;
        };

        // Vanilla Heightmap.primeHeightmaps: descend from the top of the chunk
        // and let each still-unresolved heightmap claim the first block it
        // accepts, so one pass fills all three. Stores topY + 1.
        struct ColumnHeights {
            uint16_t worldSurface = 0;
            uint16_t motionBlocking = 0;
            uint16_t oceanFloor = 0;
        };

        ColumnHeights scanColumn(int x, int z, int chunkSize, int height,
                                 const std::vector<uint64_t>& data,
                                 const BlockFlagTable& table)
        {
            ColumnHeights out;
            bool gotSurface = false, gotMotion = false, gotFloor = false;
            for (int y = height - 1; y >= 0; --y) {
                uint64_t id = data[static_cast<size_t>(y) * chunkSize * chunkSize
                                   + static_cast<size_t>(z) * chunkSize + x];
                if (id == 0) continue;
                const BlockFlags& f = table.lookup(id);
                const uint16_t stored = static_cast<uint16_t>(y + 1);
                if (!gotSurface && f.nonAir)       { out.worldSurface = stored; gotSurface = true; }
                if (!gotMotion && f.motionBlocks)  { out.motionBlocking = stored; gotMotion = true; }
                if (!gotFloor && f.oceanFloor)     { out.oceanFloor = stored; gotFloor = true; }
                if (gotSurface && gotMotion && gotFloor) break;
            }
            return out;
        }

    } // namespace

    Chunk::Chunk(glm::ivec2 gridPos, int verticesPerAxis, float spacing, int height)
        : gridPos_(gridPos)
        , verticesPerAxis_(verticesPerAxis)
        , spacing_(spacing)
        , height_(height)
        , minY_(RendererSettings::get().minY) {
        float size = static_cast<float>(verticesPerAxis) * spacing;
        worldOrigin_ = glm::vec3(
            static_cast<float>(gridPos.x) * size,
            static_cast<float>(minY_),
            static_cast<float>(gridPos.y) * size
        );
        int subCount = (height + static_cast<int>(SUBCHUNK_H) - 1) / static_cast<int>(SUBCHUNK_H);
        subChunks_.reserve(subCount);
        subOccupancy_.assign(static_cast<size_t>(subCount), 0);
        for (int i = 0; i < subCount; ++i) {
            SubChunk sc{};
            sc.yBase = i * static_cast<int>(SUBCHUNK_H);
            subChunks_.push_back(std::move(sc));
        }
    }

    void Chunk::setBlockData(BlockDataPtr data, int chunkSize, int height) {
        blockData_ = std::move(data);
        chunkSize_ = chunkSize;
        height_ = height;
        const size_t columns = static_cast<size_t>(chunkSize) * chunkSize;
        worldSurfaceHeightMap_.assign(columns, 0);
        motionBlockingHeightMap_.assign(columns, 0);
        oceanFloorHeightMap_.assign(columns, 0);
        rebuildHeightMaps();
        updateOccupancy(*blockData_);
    }

    uint64_t Chunk::getBlock(int x, int y, int z) const {
        if (!blockData_ || blockData_->empty()) return 0;
        // The grid is indexed by LOCAL Y. A negative or over-height value would
        // cast to a huge size_t and read far outside the buffer, so it is
        // rejected here rather than trusted from the caller -- World::getBlock
        // is the boundary that converts, but nothing stops the next caller from
        // being a block taller than it should be.
        if (x < 0 || x >= chunkSize_ || y < 0 || y >= height_ || z < 0 || z >= chunkSize_)
            return 0;
        return (*blockData_)[static_cast<size_t>(y) * chunkSize_ * chunkSize_
                             + static_cast<size_t>(z) * chunkSize_
                             + static_cast<size_t>(x)];
    }

    void Chunk::setBlock(int x, int y, int z, uint64_t blockId) {
        if (!blockData_ || blockData_->empty()) return;
        if (x < 0 || x >= chunkSize_ || y < 0 || y >= height_ || z < 0 || z >= chunkSize_)
            return;

        // Copy-on-write: blockData_ is shared with blockCache_ (and possibly a
        // pending mesh task). Edits are rare, so clone the buffer, mutate the
        // clone, then publish the new handle — the old buffer stays alive for
        // whoever still holds it.
        std::vector<uint64_t> data = *blockData_;
        data[static_cast<size_t>(y) * chunkSize_ * chunkSize_
             + static_cast<size_t>(z) * chunkSize_
             + static_cast<size_t>(x)] = blockId;

        // Heightmaps, occupancy and the vertical extremes are all derived from
        // the grid, so a single changed column is rescanned in full. A rescan
        // is a few hundred reads and user edits are rare, which beats keeping
        // three independent incremental heightmaps correct by hand.
        refreshColumn(x, z, data);

        blockData_ = std::make_shared<const std::vector<uint64_t>>(std::move(data));

        int subIdx = y / static_cast<int>(SUBCHUNK_H);
        if (static_cast<size_t>(subIdx) < subChunks_.size())
            subChunks_[subIdx].meshNeeded = true;
        if (y % static_cast<int>(SUBCHUNK_H) == 0 && subIdx > 0)
            subChunks_[subIdx - 1].meshNeeded = true;
        if (y % static_cast<int>(SUBCHUNK_H) == static_cast<int>(SUBCHUNK_H) - 1 &&
            static_cast<size_t>(subIdx + 1) < subChunks_.size())
            subChunks_[subIdx + 1].meshNeeded = true;
    }

    bool Chunk::isRemeshNeeded() const {
        for (auto& sub : subChunks_)
            if (sub.meshNeeded) return true;
        return false;
    }

    void Chunk::markDirty() {
        for (auto& sub : subChunks_)
            sub.meshNeeded = true;
    }

    void Chunk::markRemeshed() {
        for (auto& sub : subChunks_)
            sub.meshNeeded = false;
    }

    void Chunk::rebuildHeightMaps() {
        if (chunkSize_ == 0 || !blockData_) return;
        if (worldSurfaceHeightMap_.empty()) return;

        BlockFlagTable table;
        table.build();
        const auto& data = *blockData_;

        for (int z = 0; z < chunkSize_; ++z) {
            for (int x = 0; x < chunkSize_; ++x) {
                ColumnHeights h = scanColumn(x, z, chunkSize_, height_, data, table);
                const size_t idx = static_cast<size_t>(z) * chunkSize_ + x;
                worldSurfaceHeightMap_[idx] = h.worldSurface;
                motionBlockingHeightMap_[idx] = h.motionBlocking;
                oceanFloorHeightMap_[idx] = h.oceanFloor;
            }
        }

        // Culling bounds span every non-air cell, so the two ends come from
        // different maps:
        //   top    <- world surface (topmost non-air; water counts, since the
        //              water surface is drawn and must stay inside the box)
        //   bottom <- ocean floor (topmost solid; the seabed)
        // Underwater those two diverge by the whole water depth. Taking the
        // bottom from the surface map too left the box a one-block sliver on
        // the waterline, far above the seabed geometry that is actually on
        // screen, and the frustum culled the chunk.
        maxHeight_ = 0;
        minHeight_ = static_cast<uint16_t>(height_);
        for (size_t i = 0; i < worldSurfaceHeightMap_.size(); ++i) {
            const uint16_t surface = worldSurfaceHeightMap_[i];
            if (surface == 0) continue;               // empty column
            const uint16_t top = static_cast<uint16_t>(surface - 1);
            if (top > maxHeight_) maxHeight_ = top;

            // A column can hold water with nothing solid under it (a waterfall),
            // which bounds the top but not the bottom.
            const uint16_t floorTop = oceanFloorHeightMap_[i];
            if (floorTop == 0) continue;
            const uint16_t groundTop = static_cast<uint16_t>(floorTop - 1);
            if (groundTop < minHeight_) minHeight_ = groundTop;
        }
        // Nothing solid anywhere, so there is no floor to bound against: fall
        // back to the top, which also collapses an all-air chunk to an empty box.
        if (minHeight_ > maxHeight_) minHeight_ = maxHeight_;
    }

    void Chunk::refreshColumn(int localX, int localZ, const std::vector<uint64_t>& data) {
        if (chunkSize_ == 0 || worldSurfaceHeightMap_.empty()) return;

        BlockFlagTable table;
        table.build();
        ColumnHeights h = scanColumn(localX, localZ, chunkSize_, height_, data, table);
        const size_t idx = static_cast<size_t>(localZ) * chunkSize_ + localX;
        worldSurfaceHeightMap_[idx] = h.worldSurface;
        motionBlockingHeightMap_[idx] = h.motionBlocking;
        oceanFloorHeightMap_[idx] = h.oceanFloor;

        // Only the edited column can have moved, but the vertical extremes are
        // chunk-wide, so they are recomputed from the map. 256 uint16 reads.
        // Top from the world surface map and bottom from the ocean floor map,
        // matching the initial build above.
        maxHeight_ = 0;
        minHeight_ = static_cast<uint16_t>(height_);
        for (size_t i = 0; i < worldSurfaceHeightMap_.size(); ++i) {
            const uint16_t surface = worldSurfaceHeightMap_[i];
            if (surface == 0) continue;
            const uint16_t top = static_cast<uint16_t>(surface - 1);
            if (top > maxHeight_) maxHeight_ = top;

            const uint16_t floorTop = oceanFloorHeightMap_[i];
            if (floorTop == 0) continue;
            const uint16_t groundTop = static_cast<uint16_t>(floorTop - 1);
            if (groundTop < minHeight_) minHeight_ = groundTop;
        }
        if (minHeight_ > maxHeight_) minHeight_ = maxHeight_;
    }

    void Chunk::updateOccupancy(const std::vector<uint64_t>& data) {
        if (subOccupancy_.empty()) return;
        BlockFlagTable table;
        table.build();

        const int height = static_cast<int>(subOccupancy_.size()) * static_cast<int>(SUBCHUNK_H);
        std::fill(subOccupancy_.begin(), subOccupancy_.end(), 0);
        for (int sub = 0; sub < static_cast<int>(subOccupancy_.size()); ++sub) {
            const int yStart = sub * static_cast<int>(SUBCHUNK_H);
            const int yEnd = std::min(yStart + static_cast<int>(SUBCHUNK_H), height);
            uint8_t bits = 0;
            for (int y = yStart; y < yEnd; ++y) {
                for (int z = 0; z < chunkSize_; ++z) {
                    const size_t rowBase = static_cast<size_t>(y) * chunkSize_ * chunkSize_
                                         + static_cast<size_t>(z) * chunkSize_;
                    for (int x = 0; x < chunkSize_; ++x) {
                        uint64_t id = data[rowBase + x];
                        if (id == 0) continue;
                        bits |= 1u;
                        if (table.lookup(id).transparent) bits |= 2u;
                    }
                }
            }
            subOccupancy_[static_cast<size_t>(sub)] = bits;
        }

        hasOccupiedSubChunks_ = false;
        for (size_t i = 0; i < subOccupancy_.size(); ++i) {
            if (subOccupancy_[i] & 1u) {
                if (!hasOccupiedSubChunks_) firstOccupiedSub_ = i;
                lastOccupiedSub_ = i;
                hasOccupiedSubChunks_ = true;
            }
        }
    }

    uint64_t Chunk::hashBytes(const uint8_t* data, size_t size) {
        uint64_t hash = 1469598103934665603ull;
        for (size_t i = 0; i < size; ++i) {
            hash ^= data[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    void Chunk::upload() {
        // Pass 1: count total geometry across all sub-chunks with geometry
        size_t totalVerts = 0;
        size_t totalIndices = 0;
        size_t totalTransparentVerts = 0;
        size_t totalTransparentIndices = 0;
        for (auto& sub : subChunks_) {
            if (sub.indexCount > 0) {
                totalVerts += sub.vertices.size();
                totalIndices += sub.indices.size();
            }
            if (sub.transparentIndexCount > 0) {
                totalTransparentVerts += sub.transparentVertices.size();
                totalTransparentIndices += sub.transparentIndices.size();
            }
        }

        if (totalVerts == 0 && totalIndices == 0 &&
            totalTransparentVerts == 0 && totalTransparentIndices == 0) {
            for (auto& sub : subChunks_)
                sub.meshNeeded = false;

            // Remove previously uploaded GPU buffers if the chunk now has no geometry
            if (hasUploaded_) {
                ChunkUploadData emptyData{};
                emptyData.chunkKey = makeChunkKey(gridPos_.x, gridPos_.y);

                MessageBus::Get().send(ThreadName::Renderer, [emptyData = std::move(emptyData)]() {
                    Runtime::get().renderThread->getUploader().upload(emptyData);
                });

                hasUploaded_ = false;
                lastMeshHash_ = 0;
            }
            return;
        }

        // Merge all sub-chunks with geometry into combined CPU buffers. The
        // transparent stream (water/glass) stays in its own buffer pair so the
        // renderer can draw it after the opaque pass with blending enabled.
        std::vector<ChunkVertex> combinedVerts;
        std::vector<uint16_t> combinedIndices;
        combinedVerts.reserve(totalVerts);
        combinedIndices.reserve(totalIndices);

        std::vector<ChunkVertex> combinedTransparentVerts;
        std::vector<uint16_t> combinedTransparentIndices;
        combinedTransparentVerts.reserve(totalTransparentVerts);
        combinedTransparentIndices.reserve(totalTransparentIndices);

        uint32_t baseVertex = 0;
        uint32_t baseTransparentVertex = 0;
        for (auto& sub : subChunks_) {
            if (sub.indexCount > 0) {
                combinedVerts.insert(combinedVerts.end(),
                                     sub.vertices.begin(), sub.vertices.end());
                for (auto idx : sub.indices)
                    combinedIndices.push_back(idx + baseVertex);
                baseVertex += static_cast<uint32_t>(sub.vertices.size());
            }
            if (sub.transparentIndexCount > 0) {
                combinedTransparentVerts.insert(combinedTransparentVerts.end(),
                                                sub.transparentVertices.begin(), sub.transparentVertices.end());
                for (auto idx : sub.transparentIndices)
                    combinedTransparentIndices.push_back(idx + baseTransparentVertex);
                baseTransparentVertex += static_cast<uint32_t>(sub.transparentVertices.size());
            }
        }

        // Skip re-upload when the merged geometry is unchanged
        uint64_t hash = hashBytes(
            reinterpret_cast<const uint8_t*>(combinedVerts.data()),
            combinedVerts.size() * sizeof(ChunkVertex));
        hash = hashBytes(reinterpret_cast<const uint8_t*>(combinedIndices.data()),
                         combinedIndices.size() * sizeof(uint16_t)) ^ hash;
        hash = hashBytes(reinterpret_cast<const uint8_t*>(combinedTransparentVerts.data()),
                         combinedTransparentVerts.size() * sizeof(ChunkVertex)) ^ hash;
        hash = hashBytes(reinterpret_cast<const uint8_t*>(combinedTransparentIndices.data()),
                         combinedTransparentIndices.size() * sizeof(uint16_t)) ^ hash;

        if (hasUploaded_ && hash == lastMeshHash_) {
            for (auto& sub : subChunks_)
                sub.meshNeeded = false;
            return;
        }

        ChunkUploadData uploadData{};
        uploadData.chunkKey = makeChunkKey(gridPos_.x, gridPos_.y);
        uploadData.vertices = std::move(combinedVerts);
        uploadData.indices = std::move(combinedIndices);
        uploadData.transparentVertices = std::move(combinedTransparentVerts);
        uploadData.transparentIndices = std::move(combinedTransparentIndices);

        MessageBus::Get().send(ThreadName::Renderer, [uploadData = std::move(uploadData)]() {
            Runtime::get().renderThread->getUploader().upload(uploadData);
        });

        lastMeshHash_ = hash;
        hasUploaded_ = true;

        // Sub-chunk CPU data persists for future partial rebuilds
        for (auto& sub : subChunks_) {
            sub.meshNeeded = false;
        }
    }

} // namespace kc
