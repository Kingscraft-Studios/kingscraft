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
#include <future>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>

#include "PlayerController.hpp"

namespace kc {

    class Block;

    class World {
    public:
        World(ITerrainGenerator& terrainGen, int chunkSize, int height);
        ~World();

        void unloadChunk(int gridX, int gridZ);
        bool isChunkLoaded(int gridX, int gridZ) const;

        void tick(double dt);
        void flushPendingCleanup();
        void processCompletedChunks();
        void processGateRemeshResults();

        std::vector<Chunk*> getLoadedChunks() const;

        int getChunkSize() const { return chunkSize_; }
        int getHeight() const { return height_; }

        const Chunk* getChunk(int gridX, int gridZ) const;

        bool setBlock(int worldX, int worldY, int worldZ, const Block& block);
        uint8_t getBlock(int worldX, int worldY, int worldZ) const;
        int getSurfaceHeight(int worldX, int worldZ) const;
        void remeshDirtyChunks();

        static int worldToGrid(float worldCoord, int chunkSize);

        PlayerController& getPlayerController() { return playerController_; }

    private:
        struct ChunkGenResult {
            int gx;
            int gz;
            std::unique_ptr<Chunk> chunk;
        };

        struct GateRemeshResult {
            int gx;
            int gz;
            uint8_t mask;               // gates evaluated (bits 2..5 = PosZ,NegZ,PosX,NegX)
            std::vector<SubChunk> slabs; // 1 per sub-chunk: the freshly emitted gate faces
        };

        struct ChunkEditResult {
            int gx;
            int gz;
            uint32_t mask;              // subchunks rebuilt (1 bit per subchunk)
            std::vector<SubChunk> subs; // rebuilt geometry, one per set bit (ascending)
        };

        Chunk* getChunk(int gridX, int gridZ);

        void loadChunkSync(int gridX, int gridZ);
        void noiseThreadFunc();
        void meshThreadFunc();
        void markNeighborsForBorder(int gx, int gz);
        void queueGateRemesh(int gx, int gz, int gate);
        void processEditResults();

        static constexpr int HIGH_PRIO_RADIUS = 1;
        static constexpr int MAX_REQUESTS_PER_FRAME = 50;
        static constexpr int MAX_REMESH_PER_FRAME = 16;
        static constexpr int CLEANUP_DELAY = 4;

        ITerrainGenerator& terrainGen_;
        int chunkSize_;
        int height_;
        std::unordered_map<uint64_t, std::unique_ptr<Chunk>> chunks_;
        std::array<std::vector<std::unique_ptr<Chunk>>, CLEANUP_DELAY + 1> pendingCleanup_;
        uint64_t frameCount_ = 0;

        std::shared_future<void> noiseDone_;
        std::atomic<bool> noiseRunning_{true};
        std::condition_variable noiseCV_;

        std::shared_future<void> meshDone_;
        std::atomic<bool> meshRunning_{true};
        std::condition_variable meshCV_;

        std::mutex queueMutex_;   // guards the hand-off queues both threads share

        std::deque<std::pair<int,int>> pendingGen_;
        std::deque<uint64_t> toMesh_;              // block data ready, awaiting meshing
        std::deque<ChunkGenResult> completedGen_;
        std::unordered_set<uint64_t> pendingRequests_;
        std::deque<uint64_t> remeshQueue_;
        std::deque<GateRemeshResult> completedGateRemesh_;
        std::unordered_map<uint64_t, uint8_t> remeshGates_;
        std::deque<uint64_t> editQueue_;           // block edits awaiting mesher rebuild
        std::unordered_map<uint64_t, uint32_t> editMasks_;   // queued subchunk masks per key
        std::deque<ChunkEditResult> completedEdits_;
        int remeshRequestsThisTick_ = 0;

        bool hasCenter_ = false;
        int lastCenterX_ = 0;
        int lastCenterZ_ = 0;

        // Movement history for directional generation priority.
        float prevCameraX_ = 0.0f;
        float prevCameraZ_ = 0.0f;
        float moveDirX_ = 1.0f;
        float moveDirZ_ = 0.0f;
        bool hasMoveDir_ = false;

        std::unordered_map<uint64_t, BlockDataPtr> blockCache_;
        mutable std::mutex cacheMutex_;

        // Chunks edited since load (setBlock). Only these are serialized and staged
        // for the region overlay; unedited chunks regenerate from the seed and
        // are never written to disk.
        std::unordered_set<uint64_t> unsavedChunks_;

        mutable std::vector<Chunk*> chunkCache_;
        mutable bool chunkCacheDirty_ = false;

        PlayerController playerController_;
    };

} // namespace kc
