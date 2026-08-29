#include "Core/World/Chunk.hpp"
#include <cstring>
#include <stdexcept>

#include "Bus/MessageBus.hpp"
#include "Core/World/ChunkKey.hpp"
#include "Core/World/ChunkUploadData.hpp"
#include "Threads/Renderer.hpp"

namespace lve {

    Chunk::Chunk(glm::ivec2 gridPos, int verticesPerAxis, float spacing, int height)
        : gridPos_(gridPos)
        , verticesPerAxis_(verticesPerAxis)
        , spacing_(spacing)
        , height_(height) {
        float size = static_cast<float>(verticesPerAxis) * spacing;
        worldOrigin_ = glm::vec3(
            static_cast<float>(gridPos.x) * size,
            0.0f,
            static_cast<float>(gridPos.y) * size
        );
        int subCount = (height + SUBCHUNK_H - 1) / SUBCHUNK_H;
        subChunks_.reserve(subCount);
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
        heightMap_.resize(static_cast<size_t>(chunkSize) * chunkSize);
        rebuildHeightmap();
    }

    uint8_t Chunk::getBlock(int x, int y, int z) const {
        if (!blockData_ || blockData_->empty()) return 0;
        return (*blockData_)[static_cast<size_t>(y) * chunkSize_ * chunkSize_
                             + static_cast<size_t>(z) * chunkSize_
                             + static_cast<size_t>(x)];
    }

    void Chunk::setBlock(int x, int y, int z, uint8_t blockId) {
        if (!blockData_ || blockData_->empty()) return;
        if (x < 0 || x >= chunkSize_ || y < 0 || y >= height_ || z < 0 || z >= chunkSize_)
            return;

        // Copy-on-write: blockData_ is shared with blockCache_ (and possibly a
        // pending mesh task). Edits are rare, so clone the 25KB buffer, mutate
        // the clone, then publish the new handle — the old buffer stays alive
        // for whoever still holds it.
        std::vector<uint8_t> data = *blockData_;
        data[static_cast<size_t>(y) * chunkSize_ * chunkSize_
             + static_cast<size_t>(z) * chunkSize_
             + static_cast<size_t>(x)] = blockId;

        // Update heightmap for this column
        size_t hmIdx = static_cast<size_t>(z) * chunkSize_ + static_cast<size_t>(x);
        if (blockId != 0) {
            if (static_cast<uint16_t>(y) > heightMap_[hmIdx]) {
                heightMap_[hmIdx] = static_cast<uint16_t>(y);
                if (static_cast<uint16_t>(y) > maxHeight_)
                    maxHeight_ = static_cast<uint16_t>(y);
            }
        } else {
            if (static_cast<uint16_t>(y) == heightMap_[hmIdx]) {
                uint16_t newTop = 0;
                for (int ny = y - 1; ny >= 0; --ny) {
                    if (data[static_cast<size_t>(ny) * chunkSize_ * chunkSize_
                             + static_cast<size_t>(z) * chunkSize_
                             + static_cast<size_t>(x)] != 0) {
                        newTop = static_cast<uint16_t>(ny);
                        break;
                    }
                }
                heightMap_[hmIdx] = newTop;
                if (newTop == 0 || static_cast<uint16_t>(y) == maxHeight_)
                    updateMinMaxHeight();
            }
        }

        blockData_ = std::make_shared<const std::vector<uint8_t>>(std::move(data));

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

    void Chunk::rebuildHeightmap() {
        if (chunkSize_ == 0 || !blockData_) return;
        maxHeight_ = 0;
        minHeight_ = static_cast<uint16_t>(height_);
        for (int z = 0; z < chunkSize_; ++z) {
            for (int x = 0; x < chunkSize_; ++x) {
                uint16_t top = 0;
                for (int y = height_ - 1; y >= 0; --y) {
                    if ((*blockData_)[static_cast<size_t>(y) * chunkSize_ * chunkSize_
                                     + static_cast<size_t>(z) * chunkSize_
                                     + static_cast<size_t>(x)] != 0) {
                        top = static_cast<uint16_t>(y);
                        break;
                    }
                }
                heightMap_[static_cast<size_t>(z) * chunkSize_ + static_cast<size_t>(x)] = top;
                if (top > maxHeight_) maxHeight_ = top;
                if (top < minHeight_) minHeight_ = top;
            }
        }
        if (minHeight_ == static_cast<uint16_t>(height_)) minHeight_ = 0;
    }

    void Chunk::updateMinMaxHeight() {
        if (chunkSize_ == 0) return;
        maxHeight_ = 0;
        minHeight_ = static_cast<uint16_t>(height_);
        for (size_t i = 0; i < heightMap_.size(); ++i) {
            auto h = heightMap_[i];
            if (h > maxHeight_) maxHeight_ = h;
            if (h < minHeight_) minHeight_ = h;
        }
        if (minHeight_ == static_cast<uint16_t>(height_)) minHeight_ = 0;
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
        for (auto& sub : subChunks_) {
            if (sub.indexCount == 0) continue;
            totalVerts += sub.vertices.size();
            totalIndices += sub.indices.size();
        }

        if (totalVerts == 0 || totalIndices == 0) {
            for (auto& sub : subChunks_)
                sub.meshNeeded = false;

            // Remove previously uploaded GPU buffers if the chunk now has no geometry
            if (hasUploaded_) {
                ChunkUploadData emptyData{};
                emptyData.chunkKey = makeChunkKey(gridPos_.x, gridPos_.y);

                MessageBus::Get().send(ThreadName::Renderer, [emptyData = std::move(emptyData)]() {
                    RenderThread::getInstance().getUploader().upload(emptyData);
                });

                hasUploaded_ = false;
                lastMeshHash_ = 0;
            }
            return;
        }

        // Merge all sub-chunks with geometry into combined CPU buffers
        std::vector<ChunkVertex> combinedVerts;
        std::vector<uint16_t> combinedIndices;
        combinedVerts.reserve(totalVerts);
        combinedIndices.reserve(totalIndices);

        uint32_t baseVertex = 0;
        for (auto& sub : subChunks_) {
            if (sub.indexCount == 0) continue;
            combinedVerts.insert(combinedVerts.end(),
                                 sub.vertices.begin(), sub.vertices.end());
            for (auto idx : sub.indices)
                combinedIndices.push_back(idx + baseVertex);
            baseVertex += static_cast<uint32_t>(sub.vertices.size());
        }

        // Skip re-upload when the merged geometry is unchanged
        uint64_t hash = hashBytes(
            reinterpret_cast<const uint8_t*>(combinedVerts.data()),
            combinedVerts.size() * sizeof(ChunkVertex));
        hash = hashBytes(reinterpret_cast<const uint8_t*>(combinedIndices.data()),
                         combinedIndices.size() * sizeof(uint16_t)) ^ hash;

        if (hasUploaded_ && hash == lastMeshHash_) {
            for (auto& sub : subChunks_)
                sub.meshNeeded = false;
            return;
        }

        ChunkUploadData uploadData{};
        uploadData.chunkKey = makeChunkKey(gridPos_.x, gridPos_.y);
        uploadData.vertices = std::move(combinedVerts);
        uploadData.indices = std::move(combinedIndices);

        MessageBus::Get().send(ThreadName::Renderer, [uploadData = std::move(uploadData)]() {
            RenderThread::getInstance().getUploader().upload(uploadData);
        });

        lastMeshHash_ = hash;
        hasUploaded_ = true;

        // Sub-chunk CPU data persists for future partial rebuilds
        for (auto& sub : subChunks_) {
            sub.meshNeeded = false;
        }
    }

} // namespace lve
