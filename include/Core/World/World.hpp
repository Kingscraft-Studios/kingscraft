#pragma once

#include "Core/World/Chunk.hpp"
#include "Core/World/ChunkMesher.hpp"
#include "Core/World/ITerrainGenerator.hpp"
#include <vector>
#include <array>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>

namespace lve {

    class Device;

    class World {
    public:
        World(Device& device, ITerrainGenerator& terrainGen, int chunkSize, int height);
        ~World();

        void unloadChunk(int gridX, int gridZ);
        bool isChunkLoaded(int gridX, int gridZ) const;

        void update(float cameraX, float cameraZ, int renderDistance);
        void flushPendingCleanup();
        void processCompletedChunks();

        std::vector<Chunk*> getLoadedChunks() const;

        int getChunkSize() const { return chunkSize_; }
        int getHeight() const { return height_; }

        bool setBlock(int worldX, int worldY, int worldZ, uint8_t blockId);
        uint8_t getBlock(int worldX, int worldY, int worldZ) const;
        void remeshDirtyChunks();

        static int worldToGrid(float worldCoord, int chunkSize);

    private:
        struct ChunkGenResult {
            int gx;
            int gz;
            std::vector<ChunkVertex> vertices;
            std::vector<uint16_t> indices;
            std::vector<uint8_t> blockData;
        };

        Chunk* getChunk(int gridX, int gridZ);
        static uint64_t packKey(int gx, int gz);
        void loadChunkSync(int gridX, int gridZ);
        void genThreadFunc();

        static constexpr int HIGH_PRIO_RADIUS = 1;
        static constexpr int MAX_REQUESTS_PER_FRAME = 50;
        static constexpr int CLEANUP_DELAY = 4;

        Device& device_;
        ITerrainGenerator& terrainGen_;
        int chunkSize_;
        int height_;
        std::unordered_map<uint64_t, std::unique_ptr<Chunk>> chunks_;
        std::array<std::vector<std::unique_ptr<Chunk>>, CLEANUP_DELAY + 1> pendingCleanup_;
        uint64_t frameCount_ = 0;

        std::thread genThread_;
        std::atomic<bool> genRunning_{true};
        std::mutex genMutex_;
        std::condition_variable genCV_;
        std::deque<std::pair<int,int>> pendingGen_;
        std::deque<ChunkGenResult> completedGen_;
        std::unordered_set<uint64_t> pendingRequests_;

        std::unordered_map<uint64_t, std::vector<uint8_t>> blockCache_;
        mutable std::mutex cacheMutex_;

        mutable std::vector<Chunk*> chunkCache_;
        mutable bool chunkCacheDirty_ = false;
    };

} // namespace lve
