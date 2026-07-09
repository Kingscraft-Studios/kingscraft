#include "Core/World/Chunk.hpp"
#include <cstring>
#include <stdexcept>

namespace lve {

    Chunk::Chunk(Device& device, glm::ivec2 gridPos, int verticesPerAxis, float spacing, int height)
        : device_(device)
        , gridPos_(gridPos)
        , verticesPerAxis_(verticesPerAxis)
        , spacing_(spacing)
        , height_(height)
    {
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

    Chunk::~Chunk() {
        cleanup();
    }

    void Chunk::setBlockData(std::vector<uint8_t> data, int chunkSize, int height) {
        blockData_ = std::move(data);
        chunkSize_ = chunkSize;
        height_ = height;
    }

    uint8_t Chunk::getBlock(int x, int y, int z) const {
        if (blockData_.empty()) return 0;
        return blockData_[static_cast<size_t>(y) * chunkSize_ * chunkSize_
                          + static_cast<size_t>(z) * chunkSize_
                          + static_cast<size_t>(x)];
    }

    void Chunk::setBlock(int x, int y, int z, uint8_t blockId) {
        if (blockData_.empty()) return;
        if (x < 0 || x >= chunkSize_ || y < 0 || y >= height_ || z < 0 || z >= chunkSize_)
            return;
        blockData_[static_cast<size_t>(y) * chunkSize_ * chunkSize_
                   + static_cast<size_t>(z) * chunkSize_
                   + static_cast<size_t>(x)] = blockId;
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
            return;
        }

        prevVertexBuffer_ = std::move(vertexBuffer_);
        prevIndexBuffer_ = std::move(indexBuffer_);
        prevIndexCount_ = indexCount_;

        VkDeviceSize vertexSize = totalVerts * sizeof(ChunkVertex);
        VkDeviceSize indexSize = totalIndices * sizeof(uint16_t);

        vertexBuffer_ = std::make_unique<Buffer>(
            device_, vertexSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        indexBuffer_ = std::make_unique<Buffer>(
            device_, indexSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

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

        // Copy to staging and submit transfer
        StagingAllocation staging = device_.getStagingArena().alloc(vertexSize + indexSize);
        std::memcpy(staging.data, combinedVerts.data(), vertexSize);
        std::memcpy(static_cast<char*>(staging.data) + vertexSize,
                    combinedIndices.data(), indexSize);

        if (uploadCompleteFence_ == VK_NULL_HANDLE) {
            VkFenceCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            vkCreateFence(device_.device(), &info, nullptr, &uploadCompleteFence_);
        } else {
            vkResetFences(device_.device(), 1, &uploadCompleteFence_);
        }

        VkCommandBuffer cmd = device_.beginSingleTimeCommands();
        uploadCmd_ = cmd;
        {
            VkBufferCopy copy{};
            copy.srcOffset = staging.offset;
            copy.size = vertexSize;
            vkCmdCopyBuffer(cmd, staging.buffer, vertexBuffer_->getHandle(), 1, &copy);
        }
        {
            VkBufferCopy copy{};
            copy.srcOffset = staging.offset + vertexSize;
            copy.size = indexSize;
            vkCmdCopyBuffer(cmd, staging.buffer, indexBuffer_->getHandle(), 1, &copy);
        }
        device_.submitAsync(cmd, uploadCompleteFence_);

        indexCount_ = static_cast<uint32_t>(totalIndices);

        // Sub-chunk CPU data persists for future partial rebuilds
        for (auto& sub : subChunks_) {
            sub.meshNeeded = false;
        }
    }

    void Chunk::bindAndDraw(VkCommandBuffer cmd) {
        if (!vertexBuffer_ || !indexBuffer_) return;

        VkBuffer vb;
        VkBuffer ib;
        uint32_t count;

        if (uploadCompleteFence_ != VK_NULL_HANDLE) {
            VkResult r = vkGetFenceStatus(device_.device(), uploadCompleteFence_);
            if (r == VK_NOT_READY) {
                if (!prevVertexBuffer_ || !prevIndexBuffer_) return;
                vb = prevVertexBuffer_->getHandle();
                ib = prevIndexBuffer_->getHandle();
                count = prevIndexCount_;
            } else {
                if (uploadCmd_ != VK_NULL_HANDLE) {
                    vkFreeCommandBuffers(device_.device(), device_.getCommandPool(), 1, &uploadCmd_);
                    uploadCmd_ = VK_NULL_HANDLE;
                }
                vkDestroyFence(device_.device(), uploadCompleteFence_, nullptr);
                uploadCompleteFence_ = VK_NULL_HANDLE;
                vb = vertexBuffer_->getHandle();
                ib = indexBuffer_->getHandle();
                count = indexCount_;
            }
        } else {
            vb = vertexBuffer_->getHandle();
            ib = indexBuffer_->getHandle();
            count = indexCount_;
        }

        VkBuffer bufs[] = {vb};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, bufs, offsets);
        vkCmdBindIndexBuffer(cmd, ib, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, count, 1, 0, 0, 0);
    }

    void Chunk::cleanup() {
        if (vertexBuffer_ || indexBuffer_ || prevVertexBuffer_ || prevIndexBuffer_) {
            vkDeviceWaitIdle(device_.device());
        }
        if (uploadCompleteFence_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_.device(), uploadCompleteFence_, nullptr);
            uploadCompleteFence_ = VK_NULL_HANDLE;
        }
        if (uploadCmd_ != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_.device(), device_.getCommandPool(), 1, &uploadCmd_);
            uploadCmd_ = VK_NULL_HANDLE;
        }
        vertexBuffer_.reset();
        indexBuffer_.reset();
        prevVertexBuffer_.reset();
        prevIndexBuffer_.reset();
    }

} // namespace lve
