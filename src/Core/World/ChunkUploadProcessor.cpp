#include "Core/World/ChunkUploadProcessor.hpp"

#include <cstring>
#include <unordered_set>

#include "Threads/Renderer.hpp"
#include "Util/LogUtils.hpp"

namespace kc {

    void GpuChunkData::cleanup(Device& device) {
        if (uploadCmd != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device.device(), device.getCommandPool(), 1, &uploadCmd);
            uploadCmd = VK_NULL_HANDLE;
        }
        uploadFence.reset();
        vertexBuffer.reset();
        indexBuffer.reset();
    }

    void ChunkUploadProcessor::cleanup() {
        gpuChunks.clear();
        pendingDestruction.clear();
    }

    void ChunkUploadProcessor::cleanup(Device& device) {
        for (auto& chunk : pendingDestruction) {
            chunk.data.cleanup(device);
        }
        pendingDestruction.clear();
        for (auto& [key, chunk] : gpuChunks) {
            chunk.cleanup(device);
        }
        gpuChunks.clear();
    }

    void ChunkUploadProcessor::upload(const ChunkUploadData &data) {
        auto& device = RenderThread::getInstance().getDevice();

        auto it = gpuChunks.find(data.chunkKey);
        if (it != gpuChunks.end()) {
            pendingDestruction.push_back({data.chunkKey, std::move(it->second), frameCount_ + CLEANUP_DELAY});
            gpuChunks.erase(it);
        }

        if (data.vertices.empty() || data.indices.empty())
            return;

        GpuChunkData gpuChunk{};

        VkDeviceSize vertexSize = data.vertices.size() * sizeof(ChunkVertex);
        VkDeviceSize indexSize = data.indices.size() * sizeof(uint16_t);

        gpuChunk.vertexBuffer = std::make_unique<Buffer>(
            device,
            vertexSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        gpuChunk.indexBuffer = std::make_unique<Buffer>(
            device,
            indexSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        StagingAllocation staging = device.getStagingArena().alloc(vertexSize + indexSize);

        std::memcpy(staging.data, data.vertices.data(), vertexSize);
        std::memcpy(static_cast<char*>(staging.data) + vertexSize, data.indices.data(), indexSize);

        VkFenceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        gpuChunk.uploadFence = std::make_unique<Fence>(device, info.flags);

        VkCommandBuffer cmd = device.beginSingleTimeCommands();
        gpuChunk.uploadCmd = cmd;
        {
            VkBufferCopy copy{};
            copy.srcOffset = staging.offset;
            copy.size = vertexSize;
            vkCmdCopyBuffer(cmd, staging.buffer, gpuChunk.vertexBuffer->getHandle(), 1, &copy);
        }
        {
            VkBufferCopy copy{};
            copy.srcOffset = staging.offset + vertexSize;
            copy.size = indexSize;
            vkCmdCopyBuffer(cmd, staging.buffer, gpuChunk.indexBuffer->getHandle(), 1, &copy);
        }

        device.submitAsync(cmd, gpuChunk.uploadFence->getHandle());

        gpuChunk.indexCount = static_cast<uint32_t>(data.indices.size());

        gpuChunks[data.chunkKey] = std::move(gpuChunk);
    }

    void ChunkUploadProcessor::unload(uint64_t chunkKey) {
        auto it = gpuChunks.find(chunkKey);
        if (it == gpuChunks.end()) return;

        pendingDestruction.push_back({chunkKey, std::move(it->second), frameCount_ + CLEANUP_DELAY});
        gpuChunks.erase(it);
    }

    bool ChunkUploadProcessor::destroyIfReady(DeferredGpuChunk& chunk) {
        if (frameCount_ < chunk.destroyAfterFrame)
            return false;

        auto& device = RenderThread::getInstance().getDevice();

        if (chunk.data.uploadFence && chunk.data.uploadFence->status() != VK_SUCCESS)
            return false;

        chunk.data.cleanup(device);
        return true;
    }

    void ChunkUploadProcessor::collectDestroyedChunks(const std::vector<TerrainDraw>& draws) {
        std::erase_if(pendingDestruction, [this](DeferredGpuChunk& chunk) {
            return destroyIfReady(chunk);
        });
        ++frameCount_;
    }
}
