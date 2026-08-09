#include "Core/World/World.hpp"
#include "Core/World/ChunkKey.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Bus/MessageBus.hpp"
#include "Core/World/Physics/Gravity.hpp"
#include "Renderer/RendererSettings.hpp"
#include "Threads/InputThread.hpp"
#include "Threads/Renderer.hpp"

namespace {

    std::vector<uint8_t> extractEdgeStrip(const std::vector<uint8_t>& src, int N, int h, int edgeDir) {
        std::vector<uint8_t> strip(static_cast<size_t>(h) * N, 0);
        if (edgeDir < 2) {
            int x = (edgeDir == 0) ? 0 : (N - 1);
            for (int y = 0; y < h; ++y)
                for (int z = 0; z < N; ++z)
                    strip[static_cast<size_t>(y) * N + z] = src[static_cast<size_t>(y) * N * N + z * N + x];
        } else {
            int z = (edgeDir == 2) ? 0 : (N - 1);
            for (int y = 0; y < h; ++y)
                for (int xc = 0; xc < N; ++xc)
                    strip[static_cast<size_t>(y) * N + xc] = src[static_cast<size_t>(y) * N * N + z * N + xc];
        }
        return strip;
    }

} // namespace

namespace lve {

    World::World(ITerrainGenerator& terrainGen, int chunkSize, int height)
        : terrainGen_(terrainGen), chunkSize_(chunkSize), height_(height) {
        genThread_ = std::thread([this]() { genThreadFunc(); });
        playerController_.init(InputThread::getInstance().getKeyBindHandler());
    }

    World::~World() {
        genRunning_ = false;
        genCV_.notify_all();
        if (genThread_.joinable()) genThread_.join();
        {
            std::lock_guard<std::mutex> lock(genMutex_);
            pendingGen_.clear();
            completedGen_.clear();
            pendingRequests_.clear();
        }
        for (auto& bucket : pendingCleanup_) {
            bucket.clear();
        }
    }

    int World::worldToGrid(float worldCoord, int chunkSize) {
        return static_cast<int>(std::floor(worldCoord / chunkSize));
    }

    bool World::isChunkLoaded(int gridX, int gridZ) const {
        return chunks_.find(makeChunkKey(gridX, gridZ)) != chunks_.end();
    }

    Chunk* World::getChunk(int gridX, int gridZ) {
        auto it = chunks_.find(makeChunkKey(gridX, gridZ));
        return (it != chunks_.end()) ? it->second.get() : nullptr;
    }

    const Chunk* World::getChunk(int gridX, int gridZ) const {
        auto it = chunks_.find(makeChunkKey(gridX, gridZ));
        return (it != chunks_.end()) ? it->second.get() : nullptr;
    }

    bool World::setBlock(int worldX, int worldY, int worldZ, uint8_t blockId) {
        if (worldY < 0 || worldY >= height_) return false;
        int gx = worldToGrid(static_cast<float>(worldX), chunkSize_);
        int gz = worldToGrid(static_cast<float>(worldZ), chunkSize_);
        Chunk* chunk = getChunk(gx, gz);
        if (!chunk) return false;
        int lx = worldX - gx * chunkSize_;
        int lz = worldZ - gz * chunkSize_;
        chunk->setBlock(lx, worldY, lz, blockId);

        if (lx == 0) { Chunk* n = getChunk(gx - 1, gz); if (n) n->markDirty(); }
        if (lx == chunkSize_ - 1) { Chunk* n = getChunk(gx + 1, gz); if (n) n->markDirty(); }
        if (lz == 0) { Chunk* n = getChunk(gx, gz - 1); if (n) n->markDirty(); }
        if (lz == chunkSize_ - 1) { Chunk* n = getChunk(gx, gz + 1); if (n) n->markDirty(); }

        return true;
    }

    uint8_t World::getBlock(int worldX, int worldY, int worldZ) const {
        if (worldY < 0 || worldY >= height_) return 0;
        int gx = worldToGrid(static_cast<float>(worldX), chunkSize_);
        int gz = worldToGrid(static_cast<float>(worldZ), chunkSize_);
        auto it = chunks_.find(makeChunkKey(gx, gz));
        if (it == chunks_.end()) return 0;
        int lx = worldX - gx * chunkSize_;
        int lz = worldZ - gz * chunkSize_;
        return it->second->getBlock(lx, worldY, lz);
    }

    int World::getSurfaceHeight(int worldX, int worldZ) const {
        int gx = worldToGrid(static_cast<float>(worldX), chunkSize_);
        int gz = worldToGrid(static_cast<float>(worldZ), chunkSize_);
        const Chunk* chunk = getChunk(gx, gz);
        if (!chunk) return -1;
        int lx = worldX - gx * chunkSize_;
        int lz = worldZ - gz * chunkSize_;
        return chunk->getHeightAt(lx, lz);
    }

    void World::remeshDirtyChunks() {
        int N = chunkSize_;
        int h = height_;

        for (auto& [key, chunk] : chunks_) {
            if (!chunk->isRemeshNeeded()) continue;

            uint64_t ukey = key;
            int gx = static_cast<int>(static_cast<int64_t>(ukey >> 32));
            int gz = static_cast<int>(static_cast<int64_t>(ukey & 0xFFFFFFFF));

            std::vector<uint8_t> edgePosX, edgeNegX, edgePosZ, edgeNegZ;
            const std::vector<uint8_t>* pEdgePosX = nullptr;
            const std::vector<uint8_t>* pEdgeNegX = nullptr;
            const std::vector<uint8_t>* pEdgePosZ = nullptr;
            const std::vector<uint8_t>* pEdgeNegZ = nullptr;

            auto itPosX = chunks_.find(makeChunkKey(gx + 1, gz));
            if (itPosX != chunks_.end()) {
                edgePosX = extractEdgeStrip(itPosX->second->getBlockData(), N, h, 0);
                pEdgePosX = &edgePosX;
            }
            auto itNegX = chunks_.find(makeChunkKey(gx - 1, gz));
            if (itNegX != chunks_.end()) {
                edgeNegX = extractEdgeStrip(itNegX->second->getBlockData(), N, h, 1);
                pEdgeNegX = &edgeNegX;
            }
            auto itPosZ = chunks_.find(makeChunkKey(gx, gz + 1));
            if (itPosZ != chunks_.end()) {
                edgePosZ = extractEdgeStrip(itPosZ->second->getBlockData(), N, h, 2);
                pEdgePosZ = &edgePosZ;
            }
            auto itNegZ = chunks_.find(makeChunkKey(gx, gz - 1));
            if (itNegZ != chunks_.end()) {
                edgeNegZ = extractEdgeStrip(itNegZ->second->getBlockData(), N, h, 3);
                pEdgeNegZ = &edgeNegZ;
            }

            for (auto& sub : chunk->getSubChunks()) {
                if (!sub.meshNeeded) continue;
                ChunkMesher::generateSubChunk(sub, chunk->getBlockData(), N, h, sub.yBase,
                                               pEdgePosX, pEdgeNegX, pEdgePosZ, pEdgeNegZ);
            }
            chunk->upload();
        }
    }

    void World::loadChunkSync(int gridX, int gridZ) {
        if (isChunkLoaded(gridX, gridZ)) return;

        int N = chunkSize_;
        int h = height_;

        std::vector<uint8_t> blockIds = terrainGen_.generateBlocks(gridX, gridZ, N, h);

        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            blockCache_[makeChunkKey(gridX, gridZ)] = blockIds;
        }

        std::vector<uint8_t> edgePosX, edgeNegX, edgePosZ, edgeNegZ;
        const std::vector<uint8_t>* pEdgePosX = nullptr;
        const std::vector<uint8_t>* pEdgeNegX = nullptr;
        const std::vector<uint8_t>* pEdgePosZ = nullptr;
        const std::vector<uint8_t>* pEdgeNegZ = nullptr;
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto it = blockCache_.find(makeChunkKey(gridX + 1, gridZ));
            if (it != blockCache_.end()) { edgePosX = extractEdgeStrip(it->second, N, h, 0); pEdgePosX = &edgePosX; }
            it = blockCache_.find(makeChunkKey(gridX - 1, gridZ));
            if (it != blockCache_.end()) { edgeNegX = extractEdgeStrip(it->second, N, h, 1); pEdgeNegX = &edgeNegX; }
            it = blockCache_.find(makeChunkKey(gridX, gridZ + 1));
            if (it != blockCache_.end()) { edgePosZ = extractEdgeStrip(it->second, N, h, 2); pEdgePosZ = &edgePosZ; }
            it = blockCache_.find(makeChunkKey(gridX, gridZ - 1));
            if (it != blockCache_.end()) { edgeNegZ = extractEdgeStrip(it->second, N, h, 3); pEdgeNegZ = &edgeNegZ; }
        }

        auto chunk = std::make_unique<Chunk>(glm::ivec2(gridX, gridZ), N, 1.0f, h);
        chunk->setBlockData(blockIds, N, h);
        for (auto& sub : chunk->getSubChunks()) {
            ChunkMesher::generateSubChunk(sub, chunk->getBlockData(), N, h, sub.yBase,
                                           pEdgePosX, pEdgeNegX, pEdgePosZ, pEdgeNegZ);
        }
        chunk->upload();

        chunks_[makeChunkKey(gridX, gridZ)] = std::move(chunk);
        chunkCacheDirty_ = true;
    }

    void World::unloadChunk(int gridX, int gridZ) {
        uint64_t key = makeChunkKey(gridX, gridZ);  // compute once

        auto it = chunks_.find(key);
        if (it == chunks_.end()) return;

        int idx = static_cast<int>(frameCount_ % pendingCleanup_.size());
        pendingCleanup_[idx].push_back(std::move(it->second));
        chunks_.erase(it);

        // NEW: Signal render thread to defer GPU cleanup
        MessageBus::Get().send(ThreadName::Renderer, [key]() {
            RenderThread::getInstance().getUploader().unload(key);
        });

        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            blockCache_.erase(key);
        }
        chunkCacheDirty_ = true;
    }

    void World::flushPendingCleanup() {
        if (frameCount_ >= CLEANUP_DELAY) {
            int clearIdx = static_cast<int>((frameCount_ - CLEANUP_DELAY) % pendingCleanup_.size());
            pendingCleanup_[clearIdx].clear();
        }
        ++frameCount_;
    }

    void World::genThreadFunc() {
        while (genRunning_) {
            std::pair<int, int> task;
            {
                std::unique_lock<std::mutex> lock(genMutex_);
                if (!genCV_.wait_for(lock, std::chrono::milliseconds(50),
                    [this]() { return !pendingGen_.empty() || !genRunning_; })) {
                    continue;
                }
                if (!genRunning_) break;
                task = pendingGen_.front();
                pendingGen_.pop_front();
            }

            int gx = task.first;
            int gz = task.second;
            int N = chunkSize_;
            int h = height_;

            std::vector<uint8_t> blockIds = terrainGen_.generateBlocks(gx, gz, N, h);

            {
                std::lock_guard<std::mutex> lock(cacheMutex_);
                blockCache_[makeChunkKey(gx, gz)] = blockIds;
            }

            std::vector<uint8_t> edgePosX, edgeNegX, edgePosZ, edgeNegZ;
            const std::vector<uint8_t>* pEdgePosX = nullptr;
            const std::vector<uint8_t>* pEdgeNegX = nullptr;
            const std::vector<uint8_t>* pEdgePosZ = nullptr;
            const std::vector<uint8_t>* pEdgeNegZ = nullptr;
            {
                std::lock_guard<std::mutex> lock(cacheMutex_);
                auto it = blockCache_.find(makeChunkKey(gx + 1, gz));
                if (it != blockCache_.end()) { edgePosX = extractEdgeStrip(it->second, N, h, 0); pEdgePosX = &edgePosX; }
                it = blockCache_.find(makeChunkKey(gx - 1, gz));
                if (it != blockCache_.end()) { edgeNegX = extractEdgeStrip(it->second, N, h, 1); pEdgeNegX = &edgeNegX; }
                it = blockCache_.find(makeChunkKey(gx, gz + 1));
                if (it != blockCache_.end()) { edgePosZ = extractEdgeStrip(it->second, N, h, 2); pEdgePosZ = &edgePosZ; }
                it = blockCache_.find(makeChunkKey(gx, gz - 1));
                if (it != blockCache_.end()) { edgeNegZ = extractEdgeStrip(it->second, N, h, 3); pEdgeNegZ = &edgeNegZ; }
            }

            auto tempChunk = std::make_unique<Chunk>(glm::ivec2(gx, gz), N, 1.0f, h);
            tempChunk->setBlockData(blockIds, N, h);
            for (auto& sub : tempChunk->getSubChunks()) {
                ChunkMesher::generateSubChunk(sub, tempChunk->getBlockData(), N, h, sub.yBase,
                                               pEdgePosX, pEdgeNegX, pEdgePosZ, pEdgeNegZ);
            }

            ChunkGenResult result;
            result.gx = gx;
            result.gz = gz;
            result.chunk = std::move(tempChunk);

            {
                std::lock_guard<std::mutex> lock(genMutex_);
                completedGen_.push_back(std::move(result));
            }
        }
    }

    void World::processCompletedChunks() {
        std::deque<ChunkGenResult> results;
        {
            std::lock_guard<std::mutex> lock(genMutex_);
            results.swap(completedGen_);
        }

        for (auto& r : results) {
            uint64_t key = makeChunkKey(r.gx, r.gz);
            {
                std::lock_guard<std::mutex> lock(genMutex_);
                pendingRequests_.erase(key);
            }

            if (chunks_.count(key)) continue;

            r.chunk->upload();
            chunks_[key] = std::move(r.chunk);
            chunkCacheDirty_ = true;
        }
    }

    void World::tick(double dt) {
        float cameraX = playerController_.getCamera().getPosition().x;
        float cameraZ = playerController_.getCamera().getPosition().z;
        int renderDistance = RendererSettings::get().renderDistance;

        int centerX = worldToGrid(cameraX, chunkSize_);
        int centerZ = worldToGrid(cameraZ, chunkSize_);

        // Unload chunks outside render distance
        std::vector<uint64_t> toUnload;
        for (auto& [key, chunk] : chunks_) {
            int gx = static_cast<int>(key >> 32);
            int gz = static_cast<int>(key & 0xFFFFFFFF);
            if (std::abs(gx - centerX) > renderDistance ||
                std::abs(gz - centerZ) > renderDistance) {
                toUnload.push_back(key);
            }
        }
        {
            std::lock_guard<std::mutex> lock(genMutex_);
            for (uint64_t key : toUnload) {
                pendingRequests_.erase(key);
            }
        }
        for (uint64_t key : toUnload) {
            int gx = static_cast<int>(key >> 32);
            int gz = static_cast<int>(key & 0xFFFFFFFF);
            unloadChunk(gx, gz);
        }

        // Load center 3x3 chunks synchronously on first update
        if (chunks_.empty()) {
            for (int gz = centerZ - HIGH_PRIO_RADIUS; gz <= centerZ + HIGH_PRIO_RADIUS; ++gz) {
                for (int gx = centerX - HIGH_PRIO_RADIUS; gx <= centerX + HIGH_PRIO_RADIUS; ++gx) {
                    loadChunkSync(gx, gz);
                }
            }
        }

        // Queue async loads in expanding rings (closest to player first)
        int queued = 0;
        {
            std::lock_guard<std::mutex> lock(genMutex_);
            for (int r = HIGH_PRIO_RADIUS + 1; r <= renderDistance; ++r) {
                auto tryQueue = [&](int gx, int gz) {
                    uint64_t key = makeChunkKey(gx, gz);
                    if (chunks_.count(key)) return;
                    if (pendingRequests_.count(key)) return;
                    if (queued >= MAX_REQUESTS_PER_FRAME) return;

                    pendingGen_.push_back({gx, gz});
                    pendingRequests_.insert(key);
                    ++queued;
                };

                // top edge: (centerZ - r)
                for (int gx = centerX - r; gx <= centerX + r; ++gx)
                    tryQueue(gx, centerZ - r);

                // bottom edge: (centerZ + r)
                for (int gx = centerX - r; gx <= centerX + r; ++gx)
                    tryQueue(gx, centerZ + r);

                // left edge: (centerX - r), skip corners
                for (int gz = centerZ - r + 1; gz <= centerZ + r - 1; ++gz)
                    tryQueue(centerX - r, gz);

                // right edge: (centerX + r), skip corners
                for (int gz = centerZ - r + 1; gz <= centerZ + r - 1; ++gz)
                    tryQueue(centerX + r, gz);

                if (queued >= MAX_REQUESTS_PER_FRAME) break;
            }
        }
        if (queued > 0) genCV_.notify_one();

        flushPendingCleanup();
        processCompletedChunks();

        if (playerController_.isCursorCaptured()) {
            playerController_.setVelocityY(Gravity::apply(playerController_.getVelocityY(), static_cast<float>(dt)));
            playerController_.tick(*this, dt);
        }
    }

    std::vector<Chunk*> World::getLoadedChunks() const {
        if (chunkCacheDirty_) {
            chunkCache_.clear();
            chunkCache_.reserve(chunks_.size());
            for (auto& [key, chunk] : chunks_) {
                chunkCache_.push_back(chunk.get());
            }
            chunkCacheDirty_ = false;
        }
        return chunkCache_;
    }

} // namespace lve
