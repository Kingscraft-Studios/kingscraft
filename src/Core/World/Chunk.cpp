#include "Core/World/Chunk.hpp"
#include <cstring>
#include <stdexcept>

namespace lve {

    Chunk::Chunk(Device& device, glm::ivec2 gridPos, int verticesPerAxis, float spacing)
        : device_(device)
        , gridPos_(gridPos)
        , verticesPerAxis_(verticesPerAxis)
        , spacing_(spacing)
    {
        float size = static_cast<float>(verticesPerAxis) * spacing;
        worldOrigin_ = glm::vec3(
            static_cast<float>(gridPos.x) * size,
            0.0f,
            static_cast<float>(gridPos.y) * size
        );
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
        remeshNeeded_ = true;
    }

    void Chunk::upload() {
        VkDeviceSize vertexSize = vertices_.size() * sizeof(ChunkVertex);
        VkDeviceSize indexSize = indices_.size() * sizeof(uint16_t);
        VkDeviceSize totalSize = vertexSize + indexSize;
        if (totalSize == 0) return;

        prevVertexBuffer_ = std::move(vertexBuffer_);
        prevIndexBuffer_ = std::move(indexBuffer_);

        if (vertexSize > 0) {
            vertexBuffer_ = std::make_unique<Buffer>(
                device_, vertexSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }
        if (indexSize > 0) {
            indexBuffer_ = std::make_unique<Buffer>(
                device_, indexSize,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }

        StagingAllocation staging = device_.getStagingArena().alloc(totalSize);
        if (vertexSize > 0) std::memcpy(staging.data, vertices_.data(), vertexSize);
        if (indexSize > 0) std::memcpy(static_cast<char*>(staging.data) + vertexSize, indices_.data(), indexSize);

        if (uploadCompleteFence_ == VK_NULL_HANDLE) {
            VkFenceCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            vkCreateFence(device_.device(), &info, nullptr, &uploadCompleteFence_);
        } else {
            vkResetFences(device_.device(), 1, &uploadCompleteFence_);
        }

        VkCommandBuffer cmd = device_.beginSingleTimeCommands();
        uploadCmd_ = cmd;
        if (vertexSize > 0) {
            VkBufferCopy copy{};
            copy.srcOffset = staging.offset;
            copy.size = vertexSize;
            vkCmdCopyBuffer(cmd, staging.buffer, vertexBuffer_->getHandle(), 1, &copy);
        }
        if (indexSize > 0) {
            VkBufferCopy copy{};
            copy.srcOffset = staging.offset + vertexSize;
            copy.size = indexSize;
            vkCmdCopyBuffer(cmd, staging.buffer, indexBuffer_->getHandle(), 1, &copy);
        }
        device_.submitAsync(cmd, uploadCompleteFence_);

        indexCount_ = static_cast<uint32_t>(indices_.size());
        vertices_.clear();
        vertices_.shrink_to_fit();
        indices_.clear();
        indices_.shrink_to_fit();
    }

    void Chunk::bindAndDraw(VkCommandBuffer cmd) {
        if (!vertexBuffer_ || !indexBuffer_) return;
        if (uploadCompleteFence_ != VK_NULL_HANDLE) {
            VkResult r = vkGetFenceStatus(device_.device(), uploadCompleteFence_);
            if (r == VK_NOT_READY) return;
            if (uploadCmd_ != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device_.device(), device_.getCommandPool(), 1, &uploadCmd_);
                uploadCmd_ = VK_NULL_HANDLE;
            }
            vkDestroyFence(device_.device(), uploadCompleteFence_, nullptr);
            uploadCompleteFence_ = VK_NULL_HANDLE;

            prevVertexBuffer_.reset();
            prevIndexBuffer_.reset();
        }

        VkBuffer vb[] = {vertexBuffer_->getHandle()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, indexBuffer_->getHandle(), 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);
    }

    void Chunk::cleanup() {
        vertexBuffer_.reset();
        indexBuffer_.reset();
        prevVertexBuffer_.reset();
        prevIndexBuffer_.reset();
        if (uploadCompleteFence_ != VK_NULL_HANDLE) {
            vkWaitForFences(device_.device(), 1, &uploadCompleteFence_, VK_TRUE, UINT64_MAX);
            vkDestroyFence(device_.device(), uploadCompleteFence_, nullptr);
            uploadCompleteFence_ = VK_NULL_HANDLE;
        }
        if (uploadCmd_ != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_.device(), device_.getCommandPool(), 1, &uploadCmd_);
            uploadCmd_ = VK_NULL_HANDLE;
        }
    }

} // namespace lve
