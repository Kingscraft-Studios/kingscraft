#pragma once

#include "Core/World/Chunk.hpp"
#include "Core/World/ChunkMesher.hpp"
#include "Core/World/ITerrainGenerator.hpp"
#include "Core/World/WorldMetadata.hpp"
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
#include <limits>

#include "PlayerController.hpp"

namespace kc {

    class Block;

    class World {
    public:
        World(ITerrainGenerator& terrainGen, int chunkSize, int height, const WorldMetadata& metadata);
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
        int getMinY() const { return minY_; }
        int getMaxY() const { return minY_ + height_ - 1; }
        // getSurfaceHeight reports minY - 1 for an unloaded column, so compare
        // against getMinY() rather than against 0.
        static constexpr int UNLOADED_SURFACE = std::numeric_limits<int>::min();

        uint64_t getWorldTime() const { return worldTime_; }

        // Incremented whenever block data that the occlusion heightmap reads
        // could change (block edits, heightmap rebuilds). Consumers use this to
        // invalidate cached per-frame results.
        uint64_t getMutationGen() const { return mutationGen_.load(std::memory_order_relaxed); }

        const Chunk* getChunk(int gridX, int gridZ) const;

        bool setBlock(int worldX, int worldY, int worldZ, const Block& block);
        uint64_t getBlock(int worldX, int worldY, int worldZ) const;
        int getSurfaceHeight(int worldX, int worldZ) const;
        void remeshDirtyChunks();
        // Marks every loaded chunk dirty (all subchunks) and queues a full
        // remesh; used after a registry reload changes texture offsets.
        void remeshAllChunks();

        static int worldToGrid(float worldCoord, int chunkSize);

        PlayerController& getPlayerController() { return playerController_; }

        // Debug switch: when false only edited chunks are saved to regions;
        // when true every loaded chunk is serialized. Toggled at runtime (F5).
        bool getSaveAllChunks() const { return saveAllChunks_.load(std::memory_order_relaxed); }
        void setSaveAllChunks(bool on) { saveAllChunks_.store(on, std::memory_order_relaxed); }

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
            SubChunkMask mask;           // subchunks rebuilt (1 bit per subchunk)
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

        // How many threads generate chunk blocks at once.
        //
        // Generation is the long pole in streaming a view: tens of milliseconds
        // per 16x16 chunk, and nothing else in the loop comes close. It is also
        // pure CPU work on data that does not change mid-generation, so it is the
        // one place where more threads pay off directly.
        //
        // 3 is deliberate rather than hardware_concurrency-derived. Generation
        // also competes with the game thread, the mesher and the render thread,
        // so past a handful of generators the extra threads mostly take CPU away
        // from the mesher instead of finishing chunks faster. Raise it if the
        // throughput log shows the mesher idling; lower it if meshing starves.
        static constexpr int GEN_WORKERS = 3;

        ITerrainGenerator& terrainGen_;
        int chunkSize_;
        int height_;
        int minY_;
        WorldMetadata metadata_;
        // False when world.kcw failed to load (corrupt / future version): the
        // unreadable file is left untouched on shutdown instead of overwritten.
        bool persistMetadata_ = false;
        // Set once the async world.kcw load has resolved (any result). The
        // first World::tick waits for this, so an ultra-fast ENTER_WORLD click
        // can never stream chunks or run physics on the unloaded defaults.
        bool loadResolved_ = false;
        uint64_t worldTime_ = 0;
        std::unordered_map<uint64_t, std::unique_ptr<Chunk>> chunks_;
        std::array<std::vector<std::unique_ptr<Chunk>>, CLEANUP_DELAY + 1> pendingCleanup_;
        uint64_t frameCount_ = 0;

        // One entry per generation thread (GEN_WORKERS). Each waits on the same
        // noiseCV_ and pops from the same pendingGen_ queue, so the work splits
        // itself; all of them are joined in the destructor.
        std::vector<std::shared_future<void>> noiseDones_;
        std::atomic<bool> noiseRunning_{true};
        std::condition_variable noiseCV_;

        std::shared_future<void> meshDone_;
        std::atomic<bool> meshRunning_{true};
        std::condition_variable meshCV_;

        // Streaming throughput counters. Generation is now several threads and
        // meshing is still one, so the interesting question after this change is
        // no longer "is generation slow" but "has generation simply handed the
        // bottleneck to the mesher". logThroughput() answers that once every
        // couple of seconds so the answer does not have to be guessed at.
        std::atomic<uint64_t> genChunksDone_{0};
        std::atomic<uint64_t> meshChunksDone_{0};
        std::atomic<uint64_t> throughputLogMicros_{0};
        // Written only by whichever worker wins the log slot below, so these
        // need no synchronisation of their own.
        uint64_t lastGenCount_ = 0;
        uint64_t lastMeshCount_ = 0;
        void logThroughput();

        std::mutex queueMutex_;   // guards the hand-off queues both threads share

        std::deque<std::pair<int,int>> pendingGen_;
        std::deque<uint64_t> toMesh_;              // block data ready, awaiting meshing
        std::deque<ChunkGenResult> completedGen_;
        std::unordered_set<uint64_t> pendingRequests_;
        std::deque<uint64_t> remeshQueue_;
        std::deque<GateRemeshResult> completedGateRemesh_;
        std::unordered_map<uint64_t, uint8_t> remeshGates_;
        std::deque<uint64_t> editQueue_;           // block edits awaiting mesher rebuild
        std::unordered_map<uint64_t, SubChunkMask> editMasks_; // queued subchunk masks per key
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
        // are never written to disk. When saveAllChunks_ is on, every loaded
        // chunk gets staged instead.
        std::unordered_set<uint64_t> unsavedChunks_;
        std::atomic<bool> saveAllChunks_{false};

        mutable std::vector<Chunk*> chunkCache_;
        mutable bool chunkCacheDirty_ = false;

        std::atomic<uint64_t> mutationGen_{0};

        PlayerController playerController_;
    };

} // namespace kc
