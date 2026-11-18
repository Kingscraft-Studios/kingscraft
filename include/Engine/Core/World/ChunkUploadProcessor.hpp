#pragma once
#include "ChunkUploadData.hpp"
#include "GpuChunkData.hpp"
#include "Renderer/FrameScene.hpp"

namespace kc {
    class ChunkUploadProcessor {
    public:
        ~ChunkUploadProcessor() {
            gpuChunks.clear();
            pendingDestruction.clear();
        }

        void upload(const ChunkUploadData& data);
        void unload(uint64_t chunkKey);
        void collectDestroyedChunks(const std::vector<TerrainDraw>& draws);

        void cleanup();
        void cleanup(Device& device);

        const GpuChunkData* getChunkData(uint64_t chunkKey) const {
            auto it = gpuChunks.find(chunkKey);
            return (it != gpuChunks.end()) ? &it->second : nullptr;
        }

    private:
        static constexpr uint64_t CLEANUP_DELAY = 4;

        struct DeferredGpuChunk {
            uint64_t chunkKey;
            GpuChunkData data;
            uint64_t destroyAfterFrame;
        };

        bool destroyIfReady(DeferredGpuChunk& chunk);

        std::unordered_map<uint64_t, GpuChunkData> gpuChunks;
        std::vector<DeferredGpuChunk> pendingDestruction;
        uint64_t frameCount_ = 0;
    };
}
